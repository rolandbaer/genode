/*
 * \brief   Backend for end points of synchronous interprocess communication
 * \author  Martin Stein
 * \author  Stefan Kalkowski
 * \date    2012-11-30
 */

/*
 * Copyright (C) 2012-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__KERNEL__IPC_NODE_H_
#define _CORE__KERNEL__IPC_NODE_H_

/* Genode includes */
#include <util/fifo.h>

namespace Kernel {

	class Thread;

	/**
	 * Backend for end points of synchronous interprocess communication
	 */
	class Ipc_node;
}

class Kernel::Ipc_node
{
	private:

		using Queue_item = Genode::Fifo_element<Ipc_node>;
		using Queue      = Genode::Fifo<Queue_item>;

		struct In
		{
			enum State { READY, WAIT, REPLY, REPLY_NO_SENDER, DESTRUCT };

			State state { READY };
			Queue queue { };

			bool waiting() const
			{
				return state == WAIT;
			}
		};

		struct Out
		{
			enum State { READY, SEND, DESTRUCT };

			State     state { READY   };
			Ipc_node *node  { nullptr };

			bool sending() const
			{
				return state == SEND;
			}
		};

		Thread     &_thread;
		Queue_item  _queue_item { *this };
		Out         _out        { };
		In          _in         { };

		/**
		 * Receive a message from another IPC node
		 */
		void _receive_from(Ipc_node &node);

		/**
		 * Cancel an ongoing send operation
		 */
		void _cancel_send();

		/**
		 * Noncopyable
		 */
		Ipc_node(const Ipc_node&) = delete;
		const Ipc_node& operator=(const Ipc_node&) = delete;

	public:

		Ipc_node(Thread &thread);

		~Ipc_node();

		/**
		 * Return whether this IPC node is ready to send a message
		 */
		bool ready_to_send() const;

		/**
		 * Send a message and wait for the according reply
		 *
		 * \param node  targeted IPC node
		 */
		void send(Ipc_node &node);

		/**
		 * Return whether this IPC node is ready to wait for messages
		 */
		bool ready_to_wait() const;

		/**
		 * Wait until a message has arrived, or handle it if one is available
		 *
		 * \return  wether a message could be received already
		 */
		void wait();

		/**
		 * Reply to last message if there's any
		 */
		void reply();

		/**
		 * Return whether this IPC node is waiting for messages
		 */
		bool waiting() const { return _in.waiting(); }
};

#endif /* _CORE__KERNEL__IPC_NODE_H_ */
