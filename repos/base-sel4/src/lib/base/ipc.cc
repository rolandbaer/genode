/*
 * \brief  seL4 implementation of the IPC API
 * \author Norman Feske
 * \date   2014-10-14
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/ipc.h>
#include <base/log.h>
#include <base/thread.h>
#include <base/sleep.h>
#include <util/misc_math.h>

/* base-internal includes */
#include <base/internal/capability_space_sel4.h>
#include <base/internal/kernel_debugger.h>
#include <base/internal/ipc_server.h>

/* seL4 includes */
#include <sel4/sel4.h>

using namespace Genode;


/**
 * Message-register definitions
 */
enum {
	MR_IDX_EXC_CODE = 0,
	MR_IDX_NUM_CAPS = 1,
	MR_IDX_CAPS     = 2,
	MR_IDX_DATA     = MR_IDX_CAPS + Msgbuf_base::MAX_CAPS_PER_MSG,
};


static unsigned &main_rcv_sel()
{
	static unsigned _main_rcv_sel = Capability_space::alloc_rcv_sel();
	return _main_rcv_sel;
}


/**
 * Call 'fn' with a reference to the receive selector of the calling thread
 */
static void with_rcv_sel_ref(auto const &fn)
{
	/*
	 * When the function is called at the very early initialization phase, we
	 * cannot access Thread::myself()->native_thread() because the
	 * Thread object of the main thread does not exist yet. During this
	 * phase, we return a reference to the 'main_rcv_sel' variable.
	 */
	Thread * const myself_ptr = Thread::myself();
	if (!myself_ptr)
		fn(main_rcv_sel());
	else
		myself_ptr->with_native_thread(
			[&] (Native_thread &nt) { fn(nt.attr.rcv_sel); },
			[&] { ASSERT(false); /* thread w/o stack cannot execute */ });
}


static void allocate_and_define_rcv_sel()
{
	with_rcv_sel_ref([&] (unsigned &rcv_sel) {
		if (!rcv_sel)
			rcv_sel = Capability_space::alloc_rcv_sel(); });
}


/*****************************
 ** Startup library support **
 *****************************/

/**
 * Convert Genode::Msgbuf_base content into seL4 message
 *
 * \param msg  source message buffer
 */
static seL4_MessageInfo_t new_seL4_message(Msgbuf_base const &msg)
{
	/*
	 * currently only 1 CAP can be sent and received,
	 * since alloc_rcv_sel only allocates 1 slot and not MAX_CAPS_PER_MSG
	 */
	ASSERT(msg.used_caps() <= 1);

	/*
	 * Supply capabilities to kernel IPC message
	 */
	seL4_SetMR(MR_IDX_NUM_CAPS, msg.used_caps());
	unsigned sel4_sel_cnt = 0;
	for (unsigned i = 0; i < msg.used_caps(); i++) {

		Native_capability const &cap = msg.cap(i);

		if (cap.valid()) {
			Capability_space::Ipc_cap_data const ipc_cap_data =
				Capability_space::ipc_cap_data(cap);

			seL4_SetMR(MR_IDX_CAPS + i, ipc_cap_data.rpc_obj_key.value());
			seL4_SetCap(sel4_sel_cnt++, ipc_cap_data.sel.value());
		} else {
			seL4_SetMR(MR_IDX_CAPS + i, Rpc_obj_key::INVALID);
		}
	}

	/*
	 * Pad unused capability slots with invalid capabilities to avoid
	 * leakage of any information that happens to be in the IPC buffer.
	 */
	for (unsigned i = (unsigned)msg.used_caps(); i < Msgbuf_base::MAX_CAPS_PER_MSG; i++)
		seL4_SetMR(MR_IDX_CAPS + i, Rpc_obj_key::INVALID);

	/*
	 * Supply data payload
	 */
	unsigned const num_data_mwords =
		align_natural((unsigned)(msg.data_size() / sizeof(umword_t)));

	umword_t const *src = (umword_t const *)msg.data();
	for (unsigned i = 0; i < num_data_mwords; i++)
		seL4_SetMR(MR_IDX_DATA + i, *src++);

	seL4_MessageInfo_t const msg_info =
		seL4_MessageInfo_new(0, 0, sel4_sel_cnt,
		                     MR_IDX_DATA + num_data_mwords);
	return msg_info;
}


/**
 * Convert seL4 message into Genode::Msgbuf_base
 */
static void decode_seL4_message(seL4_MessageInfo_t const &msg_info,
                                Msgbuf_base &dst_msg)
{
	/**
	 * Read all required data from seL4 IPC message
	 *
	 * You must not use any Genode primitives which may corrupt the IPCBuffer
	 * during this step, e.g. Lock or RPC for output !!!
	 */
	unsigned const num_caps = min((unsigned)seL4_GetMR(MR_IDX_NUM_CAPS),
	                              (unsigned)Msgbuf_base::MAX_CAPS_PER_MSG);

	uint32_t const caps_extra     = (uint32_t)seL4_MessageInfo_get_extraCaps(msg_info);
	uint32_t const caps_unwrapped = (uint32_t)seL4_MessageInfo_get_capsUnwrapped(msg_info);
	uint32_t const num_msg_words  = (uint32_t)seL4_MessageInfo_get_length(msg_info);

	Rpc_obj_key rpc_obj_keys[Msgbuf_base::MAX_CAPS_PER_MSG];
	unsigned long arg_badges[Msgbuf_base::MAX_CAPS_PER_MSG];

	for (unsigned i = 0; i < num_caps; i++) {
		rpc_obj_keys[i] = Rpc_obj_key(seL4_GetMR(MR_IDX_CAPS + i));
		if (!rpc_obj_keys[i].valid())
			/*
			 * If rpc_obj_key is invalid, avoid calling
			 * seL4_CapData_Badge_get_Badge. It may trigger a assertion if
			 * the lowest bit is set by the garbage badge value we got.
			 */
			arg_badges[i] = Rpc_obj_key::INVALID;
		else
			arg_badges[i] = seL4_GetBadge(i);
	}

	/**
	 * Extract message data payload
	 */

	/* detect malformed message with too small header */
	if (num_msg_words >= MR_IDX_DATA) {

		/* copy data payload */
		unsigned const max_words      = (unsigned)(dst_msg.capacity() / sizeof(umword_t));
		unsigned const num_data_words = min(num_msg_words - MR_IDX_DATA, max_words);

		umword_t *dst = (umword_t *)dst_msg.data();
		for (unsigned i = 0; i < num_data_words; i++)
			*dst++ = seL4_GetMR(MR_IDX_DATA + i);

		dst_msg.data_size(num_data_words*sizeof(umword_t));
	}

	/**
	 * Now we got all data from the IPCBuffer, we may use Native_capability
	 */

	/**
	 * Construct Genode capabilities from read seL4 IPC message stored in
	 * rpc_opj_keys and arg_badges.
	 */

	size_t curr_sel4_cap_idx = 0;
	for (size_t i = 0; i < num_caps; i++) {

		Rpc_obj_key const rpc_obj_key = rpc_obj_keys[i];

		/*
		 * Detect passing of invalid capabilities as arguments
		 *
		 * The second condition of the check handles the case where a non-RPC
		 * object capability as passed as RPC argument as done by the
		 * 'Cap_session::alloc' RPC function. Here, the entrypoint capability
		 * is not an RPC-object capability but a raw seL4 endpoint selector.
		 *
		 * XXX Technically, a message may contain one invalid capability
		 *     followed by a valid one. This check would still wrongly regard
		 *     the first capability as a valid one. A better approach would
		 *     be to introduce another state to Rpc_obj_key, which would
		 *     denote a valid capability that is not an RPC-object capability.
		 *     Hence it is meaningless as a key.
		 */
		if (!rpc_obj_key.valid() && caps_extra == 0) {
			dst_msg.insert(Native_capability());
			continue;
		}

		/*
		 * RPC object key as contained in the message data is valid.
		 */

		bool const unwrapped = caps_unwrapped & (1U << curr_sel4_cap_idx);

		/* distinguish unwrapped from delegated cap */
		if (unwrapped) {

			/*
			 * Received unwrapped capability
			 *
			 * This means that the capability argument belongs to our endpoint.
			 * So it is already present within the capability space.
			 */

			ASSERT(curr_sel4_cap_idx < Msgbuf_base::MAX_CAPS_PER_MSG);
			unsigned long const arg_badge = arg_badges[curr_sel4_cap_idx];

			if (arg_badge != rpc_obj_key.value()) {
				warning("argument badge (", arg_badge, ") != RPC object key (",
				        rpc_obj_key.value(), ")");
			}

			Native_capability arg_cap = Capability_space::lookup(rpc_obj_key);

			dst_msg.insert(arg_cap);
		} if (curr_sel4_cap_idx >= caps_extra) {
			dst_msg.insert(Native_capability());
			continue;
		} else {

			/*
			 * Received delegated capability
			 *
			 * We have either received a capability that is foreign to us,
			 * or an alias for a capability that we already posses. The
			 * latter can happen in the following circumstances:
			 *
			 * - We forwarded a selector that was created by another
			 *   component. We cannot re-identify such a capability when
			 *   handed back because seL4's badge mechanism works only for
			 *   capabilities belonging to the IPC destination endpoint.
			 *
			 * - We received a selector on the IPC reply path, where seL4's
			 *   badge mechanism is not in effect.
			 */

			Native_capability arg_cap = Capability_space::lookup(rpc_obj_key);

			with_rcv_sel_ref([&] (unsigned &rcv_sel_ref) {

				if (arg_cap.valid()) {

					/*
					 * Discard the received selector and keep using the already
					 * present one.
					 *
					 * XXX We'd need to find out if both the received and the
					 *     looked-up selector refer to the same endpoint.
					 *      Unfortunaltely, seL4 lacks such a comparison operation.
					 */

					Capability_space::reset_sel(rcv_sel_ref);

					dst_msg.insert(arg_cap);

				} else {

					Capability_space::Ipc_cap_data const
						ipc_cap_data(rpc_obj_key, rcv_sel_ref);

					dst_msg.insert(Capability_space::import(ipc_cap_data));

					/*
					 * Since we keep using the received selector, we need to
					 * allocate a fresh one for the next incoming delegation.
					 */
					rcv_sel_ref = Capability_space::alloc_rcv_sel();
				}
			});
		}
		curr_sel4_cap_idx++;
	}
}


/****************
 ** IPC client **
 ****************/

Rpc_exception_code Genode::ipc_call(Native_capability dst,
                                    Msgbuf_base &snd_msg, Msgbuf_base &rcv_msg,
                                    size_t)
{
	if (!dst.valid()) {
		error("Trying to invoke an invalid capability, stop.");
		sleep_forever();
	}

	allocate_and_define_rcv_sel();

	rcv_msg.reset();

	unsigned const dst_sel = (unsigned)Capability_space::ipc_cap_data(dst).sel.value();

	/**
	 * Do not use Genode primitives after this point until the return which may
	 * alter the content of the IPCBuffer, e.g. Lock or RPC.
	 */

	seL4_MessageInfo_t const request = new_seL4_message(snd_msg);
	seL4_MessageInfo_t const reply_msg_info = seL4_Call(dst_sel, request);
	Rpc_exception_code const exc_code((int)seL4_GetMR(MR_IDX_EXC_CODE));

	decode_seL4_message(reply_msg_info, rcv_msg);

	return exc_code;
}


/****************
 ** IPC server **
 ****************/

void Genode::ipc_reply(Native_capability, Rpc_exception_code exc,
                       Msgbuf_base &snd_msg)
{
	allocate_and_define_rcv_sel();

	/**
	 * Do not use Genode primitives after this point until the return which may
	 * alter the content of the IPCBuffer, e.g. Lock or RPC.
	 */

	/* called when entrypoint thread leaves entry loop and exits */
	seL4_MessageInfo_t const reply_msg_info = new_seL4_message(snd_msg);
	seL4_SetMR(MR_IDX_EXC_CODE, exc.value);

	seL4_Reply(reply_msg_info);
}


Genode::Rpc_request Genode::ipc_reply_wait(Reply_capability const &,
                                           Rpc_exception_code      exc,
                                           Msgbuf_base            &reply_msg,
                                           Msgbuf_base            &request_msg)
{
	allocate_and_define_rcv_sel();

	seL4_Word badge = 0;

	Thread::myself()->with_native_thread([&] (Native_thread &nt) {

		seL4_CPtr const dest = nt.attr.ep_sel;

		if (exc.value == Rpc_exception_code::INVALID_OBJECT)
			reply_msg.reset();

		request_msg.reset();

		/**
		 * Do not use Genode primitives after this point until the return which may
		 * alter the content of the IPCBuffer, e.g. Lock or RPC.
		 */

		seL4_MessageInfo_t const reply_msg_info = new_seL4_message(reply_msg);
		seL4_SetMR(MR_IDX_EXC_CODE, exc.value);
		seL4_MessageInfo_t const req = seL4_ReplyRecv(dest, reply_msg_info, &badge);

		decode_seL4_message(req, request_msg);
	});
	return Rpc_request(Native_capability(), badge);
}


Ipc_server::Ipc_server()
:
	Native_capability(Capability_space::create_ep_cap(*Thread::myself()))
{ }


Ipc_server::~Ipc_server() { }
