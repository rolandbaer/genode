/*
 * \brief   Class for kernel data that is needed to manage a specific CPU
 * \author  Martin Stein
 * \author  Stefan Kalkowski
 * \date    2014-01-14
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__KERNEL__CPU_H_
#define _CORE__KERNEL__CPU_H_

/* core includes */
#include <board.h>
#include <kernel/cpu_context.h>
#include <kernel/irq.h>
#include <kernel/inter_processor_work.h>
#include <kernel/thread.h>

namespace Kernel {

	/**
	 * Class for kernel data that is needed to manage a specific CPU
	 */
	class Cpu;

	/**
	 * Provides a CPU object for every available CPU
	 */
	class Cpu_pool;
}


class Kernel::Cpu : public Core::Cpu, private Irq::Pool,
                    public Genode::List<Cpu>::Element
{
	public:

		using Context = Cpu_context;

	private:

		/**
		 * Inter-processor-interrupt object of the cpu
		 */
		struct Ipi : Irq
		{
			Cpu & cpu;
			bool  pending { false };

			/**
			 * Constructor
			 *
			 * \param cpu  cpu this IPI belongs to
			 */
			Ipi(Cpu & cpu);


			void init();


			/*********************
			 **  Irq interface  **
			 *********************/

			void occurred() override;
		};

		friend void Ipi::occurred(void);

		struct Idle_thread : Kernel::Thread
		{
			/**
			 * Construct idle context for CPU 'cpu'
			 */
			Idle_thread(Board::Address_space_id_allocator &addr_space_id_alloc,
			            Irq::Pool                         &user_irq_pool,
			            Cpu_pool                          &cpu_pool,
			            Cpu                               &cpu,
			            Pd                                &core_pd);
		};

		struct Halt_job : Cpu_context
		{
			Halt_job(Cpu &cpu)
			: Cpu_context(cpu, 0, 0) { }

			void exception(Genode::Cpu_state&) override { }
			void proceed()   override;
		} _halt_job { *this };

		enum State { RUN, HALT, SUSPEND };

		State          _state { RUN };
		unsigned const _id;
		Board::Pic     _pic;
		Timer          _timer;
		Idle_thread    _idle;
		Scheduler      _scheduler;
		Ipi            _ipi_irq;

		Inter_processor_work_list &_global_work_list;
		Inter_processor_work_list  _local_work_list {};

		void     _arch_init();
		unsigned _quota() const { return (unsigned)_timer.us_to_ticks(cpu_quota_us); }
		unsigned _fill() const  { return (unsigned)_timer.us_to_ticks(cpu_fill_us); }

	public:

		void next_state_halt()    { _state = HALT; };
		void next_state_suspend() { _state = SUSPEND; };

		State state() { return _state; }

		/**
		 * Construct object for CPU 'id'
		 */
		Cpu(unsigned                     const  id,
		    Board::Address_space_id_allocator  &addr_space_id_alloc,
		    Irq::Pool                          &user_irq_pool,
		    Cpu_pool                           &cpu_pool,
		    Pd                                 &core_pd,
		    Board::Global_interrupt_controller &global_irq_ctrl);

		/**
		 * Raise the IPI of the CPU
		 */
		void trigger_ip_interrupt();

		/**
		 * Deliver interrupt to the CPU
		 *
		 * \param irq_id  id of the interrupt that occured
		 * \returns true if the interrupt belongs to this CPU, otherwise false
		 */
		bool handle_if_cpu_local_interrupt(unsigned const irq_id);

		/**
		 * Assign 'context' to this CPU
		 */
		void assign(Context& context);

		/**
		 * Return the context that should be executed next
		 */
		Context& schedule_next_context();

		Board::Pic & pic()   { return _pic; }
		Timer      & timer() { return _timer; }

		addr_t stack_base();
		addr_t stack_start();

		/**
		 * Returns the currently scheduled context
		 */
		Context & current_context() {
			return static_cast<Context&>(_scheduler.current().helping_destination()); }

		unsigned id() const { return _id; }
		Scheduler &scheduler() { return _scheduler; }

		Irq::Pool &irq_pool() { return *this; }

		Inter_processor_work_list &work_list() {
			return _local_work_list; }

		/**
		 * Return CPU's idle thread object
		 */
		Kernel::Thread &idle_thread() { return _idle; }

		void reinit_cpu()
		{
			_arch_init();
			_state = RUN;
		}

		[[noreturn]] void panic(Genode::Cpu_state &state);
};


class Kernel::Cpu_pool
{
	private:

		Inter_processor_work_list  _global_work_list {};
		Genode::List<Cpu>          _cpus {};

		friend class Cpu;

	public:

		void
		initialize_executing_cpu(Board::Address_space_id_allocator  &addr_space_id_alloc,
		                         Irq::Pool                          &user_irq_pool,
		                         Pd                                 &core_pd,
		                         Board::Global_interrupt_controller &global_irq_ctrl);

		Cpu & cpu(unsigned const id);

		/**
		 * Return object of primary CPU
		 */
		Cpu & primary_cpu() { return *_cpus.first(); }

		void for_each_cpu(auto const &fn)
		{
			Cpu * c = _cpus.first();
			while (c) {
				fn(*c);
				c = c->next();
			}
		}

		Inter_processor_work_list & work_list() {
			return _global_work_list; }
};

#endif /* _CORE__KERNEL__CPU_H_ */
