/*
 * \brief  NOVA-specific signal-delivery mechanism
 * \author Norman Feske
 * \date   2016-01-04
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__SIGNAL_BROKER_H_
#define _CORE__INCLUDE__SIGNAL_BROKER_H_

/* Genode includes */
#include <base/tslab.h>

/* NOVA includes */
#include <nova/capability_space.h>

/* core includes */
#include <platform.h>
#include <signal_source_component.h>
#include <signal_context_slab.h>

namespace Core { class Signal_broker; }


class Core::Signal_broker
{
	private:

		Allocator                            &_md_alloc;
		Rpc_entrypoint                       &_source_ep;
		Object_pool<Signal_context_component> _obj_pool { };
		Rpc_entrypoint                       &_context_ep;
		Signal_source_component               _source;
		Capability<Signal_source>             _source_cap;
		Signal_context_slab                   _context_slab { _md_alloc };

	public:

		class Invalid_signal_source : public Exception { };

		Signal_broker(Allocator      &md_alloc,
		              Rpc_entrypoint &source_ep,
		              Rpc_entrypoint &context_ep)
		:
			_md_alloc(md_alloc),
			_source_ep(source_ep),
			_context_ep(context_ep),
			_source(&_context_ep),
			_source_cap(_source_ep.manage(&_source))
		{ }

		~Signal_broker()
		{
			/* remove source from entrypoint */
			_source_ep.dissolve(&_source);

			/* free all signal contexts */
			while (Signal_context_component *r = _context_slab.any_signal_context())
				free_context(reinterpret_cap_cast<Signal_context>(r->cap()));
		}

		Capability<Signal_source> alloc_signal_source() { return _source_cap; }

		void free_signal_source(Capability<Signal_source>) { }

		/*
		 * \throw Allocator::Out_of_memory
		 */
		Signal_context_capability
		alloc_context(Capability<Signal_source>, unsigned long imprint)
		{
			/*
			 * XXX  For now, we ignore the signal-source argument as we
			 *      create only a single receiver for each PD.
			 */

			Native_capability sm = _source.blocking_semaphore();

			if (!sm.valid()) {
				warning("signal receiver sm is not valid");
				for (;;);
				return Signal_context_capability();
			}

			Native_capability si = Capability_space::import(cap_map().insert());
			Signal_context_capability cap = reinterpret_cap_cast<Signal_context>(si);

			uint8_t res = Nova::create_si(cap.local_name(),
			                              platform_specific().core_pd_sel(),
			                              imprint, sm.local_name());
			if (res != Nova::NOVA_OK) {
				warning("creating signal failed - error ", res);
				return Signal_context_capability();
			}

			/* the _contexts_slab may throw Allocator::Out_of_memory */
			_obj_pool.insert(new (&_context_slab) Signal_context_component(cap));

			/* return unique capability for the signal context */
			return cap;
		}

		void free_context(Signal_context_capability context_cap)
		{
			Signal_context_component *context;
			auto lambda = [&] (Signal_context_component *c) {
				context = c;
				if (context) _obj_pool.remove(context);
			};
			_obj_pool.apply(context_cap, lambda);

			if (!context) {
				warning(this, " - specified signal-context capability has wrong type ",
				        Hex(context_cap.local_name()));
				return;
			}
			destroy(&_context_slab, context);

			Nova::revoke(Nova::Obj_crd(context_cap.local_name(), 0));
			cap_map().remove(context_cap.local_name(), 0);
		}

		void submit(Signal_context_capability, unsigned)
		{
			/*
			 * On NOVA, signals are submitted directly to the kernel, not
			 * by using core as a proxy.
			 */
			ASSERT_NEVER_CALLED;
		}
};

#endif /* _CORE__INCLUDE__SIGNAL_BROKER_H_ */
