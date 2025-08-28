/*
 * \brief  Paging framework
 * \author Martin Stein
 * \date   2013-11-07
 */

/*
 * Copyright (C) 2013-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__PAGER_H_
#define _CORE__PAGER_H_

/* Genode includes */
#include <base/session_label.h>
#include <base/thread.h>
#include <base/signal.h>
#include <pager/capability.h>

/* core includes */
#include <kernel/signal.h>
#include <hw/mapping.h>
#include <mapping.h>
#include <object.h>
#include <rpc_cap_factory.h>

namespace Core {

	class Platform;
	class Platform_thread;

	/**
	 * Interface used by generic region_map code
	 */
	struct Mapping;

	/**
	 * Interface between the generic paging system and the base-hw backend
	 */
	class Ipc_pager;

	/**
	 * Represents a faulter and its paging context
	 */
	class Pager_object;

	/**
	 * Paging entry point that manages a pool of pager objects
	 */
	class Pager_entrypoint;

	using Pager_capability = Capability<Pager_object>;

	enum { PAGER_EP_STACK_SIZE = sizeof(addr_t) * 2048 };

	extern void init_page_fault_handling(Rpc_entrypoint &);

	void init_pager_thread_per_cpu_memory(unsigned const cpus, void * mem);
}


class Core::Ipc_pager
{
	protected:

		Kernel::Thread_fault _fault { };

		Mapping _mapping { };

	public:

		/**
		 * Instruction pointer of current page fault
		 */
		addr_t fault_ip() const;

		/**
		 * Faulter-local fault address of current page fault
		 */
		addr_t fault_addr() const;

		/**
		 * Access direction of current page fault
		 */
		bool write_fault() const;

		/**
		 * Executable permission fault
		 */
		bool exec_fault() const; 

		/**
		 * Input mapping data as reply to current page fault
		 */
		void set_reply_mapping(Mapping m);
};


class Core::Pager_object : private Kernel_object<Kernel::Signal_context>
{
	friend class Pager_entrypoint;

	private:

		unsigned long    const _badge;
		Affinity::Location     _location;
		Cpu_session_capability _cpu_session_cap;
		Thread_capability      _thread_cap;
		Platform_thread       *_pager_thread { nullptr };

		/**
		 * User-level signal handler registered for this pager object via
		 * 'Cpu_session::exception_handler()'.
		 */
		Signal_context_capability _exception_sigh { };

		/*
		 * Noncopyable
		 */
		Pager_object(const Pager_object&) = delete;
		Pager_object& operator=(const Pager_object&) = delete;

	public:

		/**
		 * Constructor
		 *
		 * \param badge  user identifaction of pager object
		 */
		Pager_object(Cpu_session_capability cpu_session_cap,
		             Thread_capability thread_cap, addr_t const badge,
		             Affinity::Location, Session_label const&,
		             Cpu_session::Name const&);

		virtual ~Pager_object() {}

		/**
		 * User identification of pager object
		 */
		unsigned long badge() const { return _badge; }

		Affinity::Location location() { return _location; }

		/**
		 * Resume faulter
		 */
		void wake_up();

		/**
		 * Assign user-level exception handler for the pager object
		 */
		void exception_handler(Signal_context_capability sigh)
		{
			_exception_sigh = sigh;
		}

		/**
		 * Notify exception handler about the occurrence of an exception
		 */
		bool submit_exception_signal()
		{
			if (!_exception_sigh.valid()) return false;

			Signal_transmitter transmitter(_exception_sigh);
			transmitter.submit();
			return true;
		}

		/**
		 * Install information that is necessary to handle page faults
		 *
		 * \param receiver  signal receiver that receives the page faults
		 */
		void start_paging(Kernel_object<Kernel::Signal_receiver> &receiver,
		                  Platform_thread &pager_thread);

		/**
		 * Called when a page-fault finally could not be resolved
		 */
		void unresolved_page_fault_occurred();

		void print(Output &out) const;

		void with_pager(auto const &fn)
		{
			if (_pager_thread) fn(*_pager_thread);
		}


		/******************
		 ** Pure virtual **
		 ******************/

		enum class Pager_result { STOP, CONTINUE };

		/**
		 * Request a mapping that resolves a fault directly
		 *
		 * \param p  offers the fault data and receives mapping data
		 *
		 * \retval   0  succeeded
		 * \retval !=0  fault can't be received directly
		 */
		virtual Pager_result pager(Ipc_pager &p) = 0;


		/***************
		 ** Accessors **
		 ***************/

		Cpu_session_capability cpu_session_cap() const { return _cpu_session_cap; }
		Thread_capability      thread_cap()      const { return _thread_cap; }

		Untyped_capability cap() {
			return Kernel_object<Kernel::Signal_context>::_cap; }
};


class Core::Pager_entrypoint
{
	private:

		friend class Platform;

		class Thread : public  Genode::Thread,
		               private Ipc_pager
		{
			private:

				friend class Pager_entrypoint;

				Kernel_object<Kernel::Signal_receiver> _kobj;

			public:

				explicit Thread(Affinity::Location);


				/**********************
				 ** Thread interface **
				 **********************/

				void entry() override;
		};

		unsigned const _cpus;
		Thread        *_threads;

	public:

		explicit Pager_entrypoint(Rpc_cap_factory &);

		/**
		 * Associate pager object 'obj' with entry point
		 */
		Pager_capability manage(Pager_object &obj);

		/**
		 * Dissolve pager object 'obj' from entry point
		 */
		void dissolve(Pager_object &obj);
};

#endif /* _CORE__PAGER_H_ */
