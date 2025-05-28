/*
 * \brief   Thread facility
 * \author  Norman Feske
 * \date    2015-05-01
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__PLATFORM_THREAD_H_
#define _CORE__INCLUDE__PLATFORM_THREAD_H_

/* Genode includes */
#include <base/thread_state.h>
#include <util/string.h>
#include <base/trace/types.h>

/* core includes */
#include <pager.h>
#include <ipc_pager.h>
#include <thread_sel4.h>
#include <install_mapping.h>
#include <assertion.h>

namespace Core {

	class Platform_pd;
	class Platform_thread;
}


class Core::Platform_thread : public List<Platform_thread>::Element
{
	private:

		/*
		 * Noncopyable
		 */
		Platform_thread(Platform_thread const &);
		Platform_thread &operator = (Platform_thread const &);

		Pager_object *_pager = nullptr;

		String<128> _name;

		/**
		 * Virtual address of the IPC buffer within the PDs address space
		 *
		 * The value for the PD's main thread is INITIAL_IPC_BUFFER_VIRT.
		 * For all other threads, the value is somewhere within the stack area.
		 */
		Utcb_virt const _utcb;

		Thread_info _info { };

		Cap_sel _pager_obj_sel { 0 };

		/*
		 * Selectors within the PD's CSpace
		 *
		 * Allocated when the thread is started.
		 */
		Cap_sel _fault_handler_sel { 0 };
		Cap_sel _ep_sel            { 0 };
		Cap_sel _lock_sel          { 0 };
		Cap_sel _vcpu_sel          { 0 };
		Cap_sel _vcpu_notify_sel   { 0 };

		friend class Platform_pd;

		Platform_pd &_pd;

		enum { INITIAL_IPC_BUFFER_VIRT = 0x1000 };

		Affinity::Location _location;
		uint16_t           _priority;

		bool _bound_to_pd = false;

		bool main_thread() const { return _utcb.addr == INITIAL_IPC_BUFFER_VIRT; }

	public:

		using Constructed = Attempt<Ok, Alloc_error>;

		Constructed constructed = Alloc_error::DENIED;

		/**
		 * Constructor
		 */
		Platform_thread(Platform_pd &pd, Rpc_entrypoint &, Ram_allocator &,
		                Local_rm &, size_t, const char *name, unsigned priority,
		                Affinity::Location, addr_t utcb);

		/**
		 * Destructor
		 */
		~Platform_thread();

		/**
		 * Return true if thread creation succeeded
		 */
		bool valid() const { return _bound_to_pd; }

		/**
		 * Start thread
		 *
		 * \param ip      instruction pointer to start at
		 * \param sp      stack pointer to use
		 * \param cpu_no  target cpu
		 */
		void start(void *ip, void *sp, unsigned int cpu_no = 0);

		/**
		 * Pause this thread
		 */
		void pause();

		/**
		 * Enable/disable single stepping
		 */
		void single_step(bool) { }

		/**
		 * Resume this thread
		 */
		void resume();

		/**
		 * Override thread state with 's'
		 */
		void state(Thread_state s);

		/**
		 * Read thread state
		 */
		Thread_state state();

		/**
		 * Return execution time consumed by the thread
		 */
		Trace::Execution_time execution_time() const;


		/************************
		 ** Accessor functions **
		 ************************/

		void pager(Pager_object &pager) { _pager = &pager; }

		Pager_object &pager()
		{
			if (_pager)
				return *_pager;

			ASSERT_NEVER_CALLED;
		}

		/**
		 * Return identification of thread when faulting
		 */
		unsigned long pager_object_badge() const { return _pager_obj_sel.value(); }

		/**
		 * Set the executing CPU for this thread
		 */
		void affinity(Affinity::Location location);

		/**
		 * Get the executing CPU for this thread
		 */
		Affinity::Location affinity() const { return _location; }

		/**
		 * Set CPU quota of the thread
		 */
		void quota(size_t) { /* not supported */ }

		/**
		 * Get thread name
		 */
		const char *name() const { return _name.string(); }


		/*****************************
		 ** seL4-specific interface **
		 *****************************/

		Cap_sel tcb_sel() const { return _info.tcb_sel; }

		bool install_mapping(Mapping const &mapping);

		void setup_vcpu(Cap_sel ept, Cap_sel notification);
};

#endif /* _CORE__INCLUDE__PLATFORM_THREAD_H_ */
