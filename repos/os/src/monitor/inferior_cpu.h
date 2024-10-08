/*
 * \brief  CPU session of monitored child PD
 * \author Norman Feske
 * \date   2023-05-08
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INFERIOR_CPU_H_
#define _INFERIOR_CPU_H_

/* local includes */
#include <monitored_cpu.h>

namespace Monitor { struct Inferior_cpu; }


struct Monitor::Inferior_cpu : Monitored_cpu_session
{
	Allocator      &_alloc;
	Thread_monitor &_thread_monitor;

	Constructible<Monitored_native_cpu_nova> _native_cpu_nova { };

	enum class Kernel { GENERIC, NOVA };

	void init_native_cpu(Kernel kernel)
	{
		if (kernel == Kernel::NOVA) {
			Capability<Native_cpu_nova> native_cpu_cap =
				reinterpret_cap_cast<Native_cpu_nova>(_real.call<Rpc_native_cpu>());
			_native_cpu_nova.construct(_ep, native_cpu_cap, "");
		}
	}

	Inferior_cpu(Entrypoint &ep, Capability<Cpu_session> real,
	             Name const &name, Allocator &alloc,
	             Thread_monitor &thread_monitor)
	:
		Monitored_cpu_session(ep, real, name), _alloc(alloc),
		_thread_monitor(thread_monitor)
	{ }


	/***************************
	 ** Cpu_session interface **
	 ***************************/

	Create_thread_result
	create_thread(Capability<Pd_session> pd, Cpu_session::Name const &name,
	              Affinity::Location affinity, Weight weight, addr_t utcb) override
	{
		Create_thread_result result = Create_thread_error::DENIED;

		Inferior_pd::with_inferior_pd(_ep, pd,

			[&] (Inferior_pd &inferior_pd) {
				_real.call<Rpc_create_thread>(inferior_pd._real, name, affinity,
				                              weight, utcb).with_result(

					[&] (Thread_capability real_thread) {

						Threads::Id thread_id { inferior_pd.alloc_thread_id() };
						bool const wait = inferior_pd._policy.wait
						               && (thread_id == Threads::Id { 1 });

						Monitored_thread &monitored_thread = *new (_alloc)
							Monitored_thread(_ep, real_thread, name,
							                 inferior_pd._threads, thread_id,
							                 pd, _thread_monitor, wait);

						result = monitored_thread.cap();
					},
					[&] (Create_thread_error e) { result = e; }
				);
			},
			[&] {
				result = _real.call<Rpc_create_thread>(pd, name, affinity, weight, utcb);
		});
		return result;
	}

	void kill_thread(Thread_capability thread) override
	{
		Monitored_thread::with_thread(_ep, thread,
			[&] (Monitored_thread &monitored_thread) {
				_real.call<Rpc_kill_thread>(monitored_thread._real);
				destroy(_alloc, &monitored_thread); },
			[&] {
				_real.call<Rpc_kill_thread>(thread); }
		);
	}

	void exception_sigh(Signal_context_capability sigh) override {
		_real.call<Rpc_exception_sigh>(sigh); }

	Affinity::Space affinity_space() const override {
		return _real.call<Rpc_affinity_space>(); }

	Dataspace_capability trace_control() override {
		return _real.call<Rpc_trace_control>(); }

	Quota quota() override { return _real.call<Rpc_quota>(); }

	Capability<Native_cpu> native_cpu() override
	{
		if (_native_cpu_nova.constructed()) {
			Untyped_capability cap = _native_cpu_nova->cap();
			return reinterpret_cap_cast<Native_cpu>(cap);
		}

		return _real.call<Rpc_native_cpu>();
	}
};

#endif /* _INFERIOR_CPU_H_ */
