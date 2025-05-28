/*
 * \brief  Core-specific instance of the IRQ session interface
 * \author Christian Helmuth
 * \date   2007-09-13
 */

/*
 * Copyright (C) 2007-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__IRQ_OBJECT_H_
#define _CORE__INCLUDE__IRQ_OBJECT_H_

/* Genode includes */
#include <base/thread.h>
#include <irq_session/irq_session.h>

/* base internal includes */
#include <base/internal/capability_space_sel4.h>

/* core includes */
#include <types.h>
#include <irq_args.h>
#include <cap_sel_alloc.h>

namespace Core { class Irq_object; }


class Core::Irq_object : public Thread {

	private:

		Signal_context_capability _sig_cap { };
		Blockade                  _sync_bootup { };
		unsigned                  _irq;

		Cap_sel_alloc::Cap_sel_attempt _kernel_irq_sel;
		Cap_sel_alloc::Cap_sel_attempt _kernel_notify_sel;

		void _wait_for_irq();

		void entry() override;

		long _associate(Irq_args const &);

	public:

		enum { MSI_OFFSET = 64 };

		Irq_object(unsigned irq);
		~Irq_object();

		void sigh(Signal_context_capability cap) { _sig_cap = cap; }
		void notify() { Signal_transmitter(_sig_cap).submit(1); }
		void ack_irq();

		Start_result start() override;
		bool associate(Irq_args const &);

		bool msi() const { return _irq >= MSI_OFFSET; }
};

#endif /* _CORE__INCLUDE__IRQ_OBJECT_H_ */
