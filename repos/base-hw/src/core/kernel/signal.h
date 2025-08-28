/*
 * \brief   Kernel backend for asynchronous inter-process communication
 * \author  Martin Stein
 * \author  Stefan Kalkowski
 * \date    2012-11-30
 */

/*
 * Copyright (C) 2012-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__KERNEL__SIGNAL_H_
#define _CORE__KERNEL__SIGNAL_H_

/* Genode includes */
#include <base/signal.h>

/* core includes */
#include <kernel/core_interface.h>
#include <object.h>

namespace Kernel {

	class Thread;

	/**
	 * Ability to receive signals from signal receivers
	 */
	class Signal_handler;

	/**
	 * Ability to destruct signal contexts
	 */
	class Signal_context_killer;

	/**
	 * Signal types that are assigned to a signal receiver each
	 */
	class Signal_context;

	/**
	 * Combines signal contexts to an entity that handlers can listen to
	 */
	class Signal_receiver;
}


class Kernel::Signal_handler
{
	friend class Signal_receiver;

	private:

		/*
		 * Noncopyable
		 */
		Signal_handler(Signal_handler const &);
		Signal_handler &operator = (Signal_handler const &);

		using Fifo_element = Genode::Fifo_element<Signal_handler>;

		Thread          &_thread;
		Fifo_element     _handlers_fe { *this   };
		Signal_receiver *_receiver    { nullptr };

	public:

		Signal_handler(Thread &thread);

		~Signal_handler();

		/**
		 * Stop waiting for a signal receiver
		 */
		void cancel_waiting();
};


class Kernel::Signal_context_killer
{
	friend class Signal_context;

	private:

		/*
		 * Noncopyable
		 */
		Signal_context_killer(Signal_context_killer const &);
		Signal_context_killer &operator = (Signal_context_killer const &);

		Thread         &_thread;
		Signal_context *_context { nullptr };

	public:

		Signal_context_killer(Thread &thread);

		~Signal_context_killer();

		/**
		 * Stop waiting for a signal context
		 */
		void cancel_waiting();
};


class Kernel::Signal_context
{
	friend class Signal_receiver;
	friend class Signal_context_killer;

	private:

		/*
		 * Noncopyable
		 */
		Signal_context(Signal_context const &);
		Signal_context &operator = (Signal_context const &);

		using Fifo_element = Genode::Fifo_element<Signal_context>;

		Kernel::Object          _kernel_object { *this };
		Fifo_element            _deliver_fe    { *this };
		Fifo_element            _contexts_fe   { *this };
		Signal_receiver        &_receiver;
		addr_t const            _imprint;
		Signal_context_killer * _killer        { nullptr };
		unsigned                _submits       { 0       };
		bool                    _ack           { true    };
		bool                    _killed        { false   };

		/**
		 * Tell receiver about the submits of the context if any
		 */
		void _deliverable();

		/**
		 * Called by receiver when all submits have been delivered
		 */
		void _delivered();

		/**
		 * Notice that the killer of the context has cancelled waiting
		 */
		void _killer_cancelled();

	public:

		/**
		 * Destructor
		 */
		~Signal_context();

		/**
		 * Constructor
		 *
		 * \param r        receiver that the context shall be assigned to
		 * \param imprint  userland identification of the context
		 */
		Signal_context(Signal_receiver &, addr_t const imprint);

		/**
		 * Submit the signal
		 *
		 * \param n  number of submits
		 */
		void submit(unsigned const n);

		/**
		 * Acknowledge delivery of signal
		 */
		void ack();

		/**
		 * Destruct context or prepare to do it as soon as delivery is done
		 *
		 * \param k  object that shall receive progress reports
		 */
		void kill(Signal_context_killer &k);

		/**
		 * Create a signal context and assign it to a signal receiver
		 *
		 * \param p         memory donation for the kernel signal-context object
		 * \param receiver  pointer to signal receiver kernel object
		 * \param imprint   user label of the signal context
		 *
		 * \retval capability id of the new kernel object
		 */
		static capid_t syscall_create(Core::Kernel_object<Signal_context> &c,
		                              Signal_receiver & receiver,
		                              addr_t const imprint)
		{
			return (capid_t)call(call_id_new_signal_context(), (Call_arg)&c,
			                     (Call_arg)&receiver, (Call_arg)imprint);
		}

		/**
		 * Destruct a signal context
		 *
		 * \param context  pointer to signal context kernel object
		 */
		static void syscall_destroy(Core::Kernel_object<Signal_context> &c) {
			call(call_id_delete_signal_context(), (Call_arg)&c); }

		Object &kernel_object() { return _kernel_object; }
};


class Kernel::Signal_receiver
{
	friend class Signal_context;
	friend class Signal_handler;

	private:

		using Signal = Genode::Signal;

		template <typename T> class Fifo : public Genode::Fifo<T> { };

		Kernel::Object                     _kernel_object { *this };
		Fifo<Signal_handler::Fifo_element> _handlers      { };
		Fifo<Signal_context::Fifo_element> _deliver       { };
		Fifo<Signal_context::Fifo_element> _contexts      { };

		/**
		 * Recognize that context 'c' has submits to deliver
		 */
		void _add_deliverable(Signal_context &c);

		/**
		 * Deliver as much submits as possible
		 */
		void _listen();

		/**
		 * Notice that a context of the receiver has been destructed
		 *
		 * \param c  destructed context
		 */
		void _context_destructed(Signal_context &c);

		/**
		 * Notice that handler 'h' has cancelled waiting
		 */
		void _handler_cancelled(Signal_handler &h);

		/**
		 * Assign context 'c' to the receiver
		 */
		void _add_context(Signal_context &c);

	public:

		~Signal_receiver();

		/**
		 * Let a handler 'h' wait for signals of the receiver
		 *
		 * \retval  0 succeeded
		 * \retval -1 failed
		 */
		bool add_handler(Signal_handler &h);

		/**
		 * Syscall to create a signal receiver
		 *
		 * \param p  memory donation for the kernel signal-receiver object
		 *
		 * \retval capability id of the new kernel object
		 */
		static capid_t syscall_create(Core::Kernel_object<Signal_receiver> &r) {
			return (capid_t)call(call_id_new_signal_receiver(), (Call_arg)&r); }

		/**
		 * Syscall to destruct a signal receiver
		 *
		 * \param receiver  pointer to signal receiver kernel object
		 */
		static void syscall_destroy(Core::Kernel_object<Signal_receiver> &r) {
			call(call_id_delete_signal_receiver(), (Call_arg)&r); }

		Object &kernel_object() { return _kernel_object; }
};

#endif /* _CORE__KERNEL__SIGNAL_RECEIVER_H_ */
