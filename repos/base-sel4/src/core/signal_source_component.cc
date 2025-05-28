/*
 * \brief  Implementation of the SIGNAL interface
 * \author Alexander Boettcher
 * \date   2016-07-09
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <platform.h>
#include <signal_source_component.h>

/* base-internal include */
#include <core_capability_space.h>

using namespace Core;


void Signal_source_component::release(Signal_context_component &context)
{
	if (context.enqueued())
		_signal_queue.remove(context);
}


void Signal_source_component::submit(Signal_context_component &context,
                                     unsigned long             cnt)
{
	if (!_notify.valid())
		return;

	/*
	 * If the client does not block in 'wait_for_signal', the
	 * signal will be delivered as result of the next
	 * 'wait_for_signal' call.
	 */
	context.increment_signal_cnt((int)cnt);

	if (context.enqueued())
		return;

	_signal_queue.enqueue(context);

	seL4_Signal(Capability_space::ipc_cap_data(_notify).sel.value());
}


Signal_source::Signal Signal_source_component::wait_for_signal()
{
	Signal result(0, 0);  /* just a dummy in the case of no signal pending */

	/* dequeue and return pending signal */
	_signal_queue.dequeue([&result] (Signal_context_component &context) {
		result = Signal(context.imprint(), context.cnt());
		context.reset_signal_cnt();
	});
	return result;
}


Signal_source_component::Signal_source_component(Rpc_entrypoint &ep)
:
	_entrypoint(ep)
{
	Platform        &platform   = platform_specific();
	Range_allocator &phys_alloc = platform.ram_alloc();

	_notify_phys = Untyped_memory::alloc_page(phys_alloc);

	_notify_phys.with_result([&](auto &result) {
		seL4_Untyped const service = Untyped_memory::untyped_sel(addr_t(result.ptr)).value();

		/* allocate notification object within core's CNode */
		platform.core_sel_alloc().alloc().with_result([&](auto sel) {
			auto ny_sel = Cap_sel(unsigned(sel));
			if (!create<Notification_kobj>(service, platform.core_cnode().sel(), ny_sel)) {
				platform.core_sel_alloc().free(ny_sel);
				return /* notify stays invalid */;
			}

			_notify = Capability_space::create_notification_cap(ny_sel);

			if (!_notify.valid()) {
				auto res = seL4_CNode_Delete(seL4_CapInitThreadCNode, ny_sel.value(), 32);

				if (res == seL4_NoError)
					platform_specific().core_sel_alloc().free(ny_sel);
				else
					error(__func__, " failed - leaking resources");
			}
		}, [&] (auto) { /* _notify stays invalid */ });
	}, [&] (auto) { /* _notify stays invalid */ });

	/* _notify stays invalid */
	if (!_notify.valid())
		error("Signal_source_component construction failed");
}


Signal_source_component::~Signal_source_component()
{
	if (!_notify.valid())
		return;

	/*
	 * _notify has no valid rpc_obj_key, see create_notification_cap() impl.
	 * Without it, the automatic ref counting won't destruct it, so we do
	 * it here manually.
	 */

	Cap_sel const sel = Capability_space::ipc_cap_data(_notify).sel;

	_notify = { };

	auto res = seL4_CNode_Delete(seL4_CapInitThreadCNode, sel.value(), 32);

	if (res == seL4_NoError)
		platform_specific().core_sel_alloc().free(sel);
	else
		error(__func__, " failed - leaking resources");
}
