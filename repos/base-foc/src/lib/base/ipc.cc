/*
 * \brief  Implementation of the IPC API for Fiasco.OC
 * \author Stefan Kalkowski
 * \author Norman Feske
 * \date   2009-12-03
 */

/*
 * Copyright (C) 2009-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/*
 * l4_msgtag_t (size == 1 mword) format:
 *
 *   --------------------------------------------------------------
 *  |  label  |  4 Bit flags  |  6 Bit items  |  6 Bit word count  |
 *   --------------------------------------------------------------
 */

/* Genode includes */
#include <base/ipc.h>
#include <base/ipc_msgbuf.h>
#include <base/thread.h>
#include <base/sleep.h>

/* base-internal includes */
#include <base/internal/lock_helper.h> /* for 'thread_get_my_native_id()' */
#include <base/internal/ipc_server.h>
#include <base/internal/native_utcb.h>
#include <base/internal/cap_map.h>
#include <base/internal/foc_assert.h>

/* Fiasco.OC includes */
#include <foc/syscall.h>

using namespace Genode;
using namespace Foc;


/***************
 ** Utilities **
 ***************/

enum Debug { DEBUG_MSG = 1 };


static inline bool ipc_error(l4_msgtag_t tag, bool print)
{
	Foc::l4_umword_t const ipc_error = l4_ipc_error(tag, l4_utcb());
	if (ipc_error && print) raw("Ipc error: ", ipc_error, " occurred!");
	return ipc_error;
}


/**
 * Representation of a capability during UTCB marshalling/unmarshalling
 */
struct Cap_info
{
	bool            valid = false;
	unsigned long   sel   = 0;
	Cap_index::id_t badge = 0;
};


/**
 * Copy message registers from UTCB to destination message buffer
 *
 * \return  protocol word (local name or exception code)
 */
static int extract_msg_from_utcb(l4_msgtag_t     tag,
                                 Receive_window &rcv_window,
                                 Msgbuf_base    &rcv_msg)
{
	unsigned num_msg_words = l4_msgtag_words(tag);

	l4_umword_t const *msg_words = (l4_umword_t const *)l4_utcb_mr();

	/* each message has at least the protocol word and the capability count */
	if (num_msg_words < 2)
		return 0;

	/* read badge / exception code from first message word */
	unsigned long const protocol_word = *msg_words++;

	/* read number of capability arguments from second message word */
	unsigned const num_caps = (unsigned)min(*msg_words, Msgbuf_base::MAX_CAPS_PER_MSG);
	msg_words++;

	num_msg_words -= 2;
	if (num_caps > 0 && num_msg_words < num_caps) {
		raw("unexpected end of message, capability info missing");
		return 0;
	}

	/*
	 * Extract capabilities
	 *
	 * The badges are stored in the subsequent message registers. For each
	 * valid badge, we expect one capability selector to be present in the
	 * receive window. The content of the receive window is tracked via
	 * 'sel_idx'. If we encounter an invalid badge, the sender specified
	 * an invalid capabilty as argument.
	 */
	unsigned const num_cap_sel = l4_msgtag_items(tag);

	Cap_info caps[num_caps];

	for (unsigned i = 0, sel_idx = 0; i < num_caps; i++) {

		Cap_index::id_t const badge = (Cap_index::id_t)(*msg_words++);

		if (badge == Cap_index::INVALID_ID)
			continue;

		/* received a delegated capability */
		if (sel_idx == num_cap_sel) {
			raw("missing capability selector in message");
			break;
		}

		caps[i].badge = badge;
		caps[i].valid = true;
		caps[i].sel   = rcv_window.rcv_cap_sel(sel_idx++);
	}
	num_msg_words -= num_caps;

	/* the remainder of the message contains the regular data payload */
	if ((num_msg_words)*sizeof(l4_mword_t) > rcv_msg.capacity()) {
		if (DEBUG_MSG)
			raw("receive message buffer too small");
		num_msg_words = (unsigned)(rcv_msg.capacity()/sizeof(l4_mword_t));
	}

	/* read message payload beginning from the second UTCB message register */
	l4_mword_t *dst = (l4_mword_t *)rcv_msg.data();
	for (unsigned i = 0; i < num_msg_words; i++)
		*dst++ = *msg_words++;

	rcv_msg.data_size(sizeof(l4_mword_t)*num_msg_words);

	/*
	 * Insert received capability selectors into cap map.
	 *
	 * Note that this operation pollutes the UTCB. Therefore we must perform
	 * it not before the entire message content is extracted.
	 */
	for (unsigned i = 0; i < num_caps; i++) {
		if (caps[i].valid) {
			rcv_msg.insert(Native_capability(cap_map().insert_map(caps[i].badge,
			                                                      caps[i].sel)));
		} else {
			rcv_msg.insert(Native_capability());
		}
	}

	return (int)protocol_word;
}


/**
 * Copy message registers from message buffer to UTCB and create message tag.
 *
 * \param protocol_word  badge of invoked object (when a client calls a server)
 *                       or the exception code (when a server replies to a
 *                       client)
 */
static l4_msgtag_t copy_msgbuf_to_utcb(Msgbuf_base &snd_msg,
                                       unsigned long protocol_word)
{

	unsigned const num_data_words = (unsigned)(snd_msg.data_size() / sizeof(l4_mword_t));
	unsigned const num_caps       = (unsigned)snd_msg.used_caps();

	/* validate capabilities present in the message buffer */
	for (unsigned i = 0; i < num_caps; i++) {

		Native_capability &cap = snd_msg.cap(i);
		if (!cap.valid())
			continue;

		if (!l4_msgtag_label(l4_task_cap_valid(L4_BASE_TASK_CAP, cap.data()->kcap())))
			cap = Native_capability();
	}

	/*
	 * Obtain capability info from message buffer
	 *
	 * This step must be performed prior any write operation to the UTCB
	 * because the 'Genode::Capability' operations may indirectly trigger
	 * system calls, which pollute the UTCB.
	 */
	Cap_info caps[num_caps];
	for (unsigned i = 0; i < num_caps; i++) {
		Native_capability const &cap = snd_msg.cap(i);
		if (cap.valid()) {
			caps[i].valid = true;
			caps[i].badge = (Cap_index::id_t)cap.local_name();
			caps[i].sel   = cap.data()->kcap();
		}
	}

	/*
	 * The message consists of a protocol word, the capability count, one badge
	 * value per capability, and the data payload.
	 */
	unsigned const num_msg_words  = 2 + num_caps + num_data_words;

	if (num_msg_words > L4_UTCB_GENERIC_DATA_SIZE) {
		raw("receive message buffer too small");
		return l4_msgtag(0, 0, 0, 0);
	}

	l4_mword_t *msg_words = (l4_mword_t *)l4_utcb_mr();

	*msg_words++ = protocol_word;
	*msg_words++ = num_caps;

	unsigned num_cap_sel = 0;

	for (unsigned i = 0; i < num_caps; i++) {

		/* store badge as normal message word */
		*msg_words++ = caps[i].valid ? caps[i].badge : Cap_index::INVALID_ID;

		/* setup flexpage for valid capability to delegate */
		if (caps[i].valid) {
			unsigned const idx = num_msg_words + 2*num_cap_sel;

			/* check bounds of 'l4_msg_regs_t::mr' */
			if (idx + 1 >= L4_UTCB_GENERIC_DATA_SIZE)
				break;

			l4_utcb_mr()->mr[idx]     = L4_ITEM_MAP/* | L4_ITEM_CONT*/;
			l4_utcb_mr()->mr[idx + 1] = l4_obj_fpage(caps[i].sel,
			                                         0, L4_FPAGE_RWX).raw;
			num_cap_sel++;
		}
	}

	/* store message data into UTCB message registers */
	for (unsigned i = 0; i < num_data_words; i++)
		*msg_words++ = snd_msg.word(i);

	return l4_msgtag(0, num_msg_words, num_cap_sel, 0);
}


/****************
 ** IPC client **
 ****************/

Rpc_exception_code Genode::ipc_call(Native_capability dst,
                                    Msgbuf_base &snd_msg, Msgbuf_base &rcv_msg,
                                    size_t)
{
	Receive_window rcv_window;
	rcv_window.init();
	rcv_msg.reset();

	/* copy call message to the UTCBs message registers */
	l4_msgtag_t const call_tag = copy_msgbuf_to_utcb(snd_msg, dst.local_name());

	addr_t rcv_cap_sel = rcv_window.rcv_cap_sel_base();
	for (size_t i = 0; i < Msgbuf_base::MAX_CAPS_PER_MSG; i++) {
		l4_utcb_br()->br[i] = rcv_cap_sel | L4_RCV_ITEM_SINGLE_CAP;
		rcv_cap_sel += L4_CAP_SIZE;
	}

	if (!dst.valid()) {
		raw("attempt to call on invalid capability");
		sleep_forever();
	}

	l4_msgtag_t const reply_tag =
		l4_ipc_call(dst.data()->kcap(), l4_utcb(), call_tag, L4_IPC_NEVER);

	if (ipc_error(reply_tag, DEBUG_MSG)) {
		raw("L4 IPC error");
		sleep_forever();
	}

	return Rpc_exception_code(extract_msg_from_utcb(reply_tag, rcv_window, rcv_msg));
}


/****************
 ** IPC server **
 ****************/

static bool badge_matches_label(unsigned long badge, unsigned long label)
{
	return badge == (label & (~0UL << 2));
}


void Genode::ipc_reply(Native_capability, Rpc_exception_code exc, Msgbuf_base &snd_msg)
{
	l4_msgtag_t tag = copy_msgbuf_to_utcb(snd_msg, exc.value);

	tag = l4_ipc_send(L4_SYSF_REPLY, l4_utcb(), tag, L4_IPC_SEND_TIMEOUT_0);

	ipc_error(tag, DEBUG_MSG);
}


Rpc_request Genode::ipc_reply_wait(Reply_capability const &,
                                   Rpc_exception_code      exc,
                                   Msgbuf_base            &reply_msg,
                                   Msgbuf_base            &request_msg)
{
	return Thread::myself()->with_native_thread([&] (Native_thread &nt) {

		Receive_window &rcv_window = nt.rcv_window;

		bool need_to_wait = false;

		for (;;) {

			request_msg.reset();

			/* prepare receive window in UTCB */
			addr_t rcv_cap_sel = rcv_window.rcv_cap_sel_base();
			for (size_t i = 0; i < Msgbuf_base::MAX_CAPS_PER_MSG; i++) {
				l4_utcb_br()->br[i] = rcv_cap_sel | L4_RCV_ITEM_SINGLE_CAP;
				rcv_cap_sel += L4_CAP_SIZE;
			}
			l4_utcb_br()->bdr &= ~L4_BDR_OFFSET_MASK;

			l4_msgtag_t request_tag;
			l4_umword_t label = 0; /* kernel-protected label of invoked capability */

			if (exc.value != Rpc_exception_code::INVALID_OBJECT && !need_to_wait) {

				l4_msgtag_t const reply_tag = copy_msgbuf_to_utcb(reply_msg, exc.value);

				request_tag = l4_ipc_reply_and_wait(l4_utcb(), reply_tag, &label, L4_IPC_SEND_TIMEOUT_0);
			} else {
				request_tag = l4_ipc_wait(l4_utcb(), &label, L4_IPC_NEVER);
			}

			if (ipc_error(request_tag, false)) {
				need_to_wait = true;
				continue;
			} else need_to_wait = false;

			/* copy request message from the UTCBs message registers */
			unsigned long const badge =
				extract_msg_from_utcb(request_tag, rcv_window, request_msg);

			/* ignore request if we detect a forged badge */
			if (!badge_matches_label(badge, label)) {
				raw("badge does not match label, ignoring request");
				continue;
			}

			return Rpc_request(Native_capability(), badge);
		}
	},
	[&] () -> Rpc_request {
		sleep_forever();   /* ipc_reply_wait called by uninitialized thread */
	});
}


Ipc_server::Ipc_server()
:
	Native_capability((Cap_index*)Foc::l4_utcb_tcr()->user[Foc::UTCB_TCR_BADGE])
{
	Thread::myself()->with_native_thread([&] (Native_thread &nt) {
		nt.rcv_window.init(); });
}


Ipc_server::~Ipc_server() { }


/********************
 ** Receive_window **
 ********************/

Receive_window::~Receive_window()
{
	if (_rcv_idx_base)
		cap_idx_alloc().free(_rcv_idx_base, MAX_CAPS_PER_MSG);
}


void Receive_window::init()
{
	_rcv_idx_base = cap_idx_alloc().alloc_range(MAX_CAPS_PER_MSG);
}


addr_t Receive_window::rcv_cap_sel_base()
{
	return _rcv_idx_base->kcap();
}


addr_t Receive_window::rcv_cap_sel(unsigned i)
{
	return rcv_cap_sel_base() + i*Foc::L4_CAP_SIZE;
}

