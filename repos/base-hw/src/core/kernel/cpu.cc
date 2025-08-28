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

#include <cpu/consts.h>

/* core includes */
#include <kernel/cpu.h>
#include <kernel/thread.h>
#include <kernel/irq.h>
#include <kernel/pd.h>
#include <board.h>
#include <hw/assert.h>
#include <hw/boot_info.h>
#include <hw/memory_consts.h>

using namespace Kernel;


/*****************
 ** Cpu_context **
 *****************/

void Cpu_context::_activate() { _cpu().assign(*this); }


void Cpu_context::_deactivate()
{
	_cpu().scheduler().unready(*this);
}


void Cpu_context::_yield()
{
	assert(_cpu().id() == Cpu::executing_id());
	_cpu().scheduler().yield();
}


void Cpu_context::_interrupt(Irq::Pool &user_irq_pool)
{
	/* let the IRQ controller take a pending IRQ for handling, if any */
	unsigned irq_id;
	if (_cpu().pic().take_request(irq_id))

		/* let the CPU of this context handle the IRQ if it is a CPU-local one */
		if (!_cpu().handle_if_cpu_local_interrupt(irq_id)) {

			/* it isn't a CPU-local IRQ, so, it must be a user IRQ */
			User_irq * irq = User_irq::object(user_irq_pool, irq_id);
			if (irq) irq->occurred();
			else Genode::raw("Unknown interrupt ", irq_id);
		}

	/* let the IRQ controller finish the currently taken IRQ */
	_cpu().pic().finish_request();
}


Cpu_context::Cpu_context(Cpu &cpu, Group_id const id)
:
	Context(id), _cpu_ptr(&cpu) { }


Cpu_context::~Cpu_context()
{
	assert(_cpu().id() == Cpu::executing_id() ||
	       &_cpu().current_context() != this);
	_deactivate();
}


/*********
 ** Cpu **
 *********/

extern "C" void idle_thread_main(void);


Cpu::Idle_thread::Idle_thread(Board::Address_space_id_allocator &addr_space_id_alloc,
                              Irq::Pool                         &user_irq_pool,
                              Cpu_pool                          &cpu_pool,
                              Cpu                               &cpu,
                              Pd                                &core_pd)
:
	Thread { addr_space_id_alloc, user_irq_pool, cpu_pool, cpu,
	         core_pd, Scheduler::Group_id::INVALID, "idle", Thread::IDLE }
{
	regs->ip = (addr_t)&idle_thread_main;
	Thread::_pd = &core_pd;
}


void Cpu::assign(Context &context)
{
	_scheduler.ready(static_cast<Scheduler::Context&>(context));
	if (_id != executing_id()) trigger_ip_interrupt();
}


bool Cpu::handle_if_cpu_local_interrupt(unsigned const irq_id)
{
	Irq * const irq = object(irq_id);

	if (!irq)
		return false;

	irq->occurred();
	return true;
}


Cpu::Context & Cpu::schedule_next_context()
{
	if (_state == SUSPEND || _state == HALT)
		return _halt_job;

	_scheduler.update();
	return current_context();
}


addr_t Cpu::stack_base()
{
	return Hw::Mm::cpu_local_memory().base +
	       Hw::Mm::CPU_LOCAL_MEMORY_SLOT_SIZE*_id;
}


addr_t Cpu::stack_start()
{
	return Abi::stack_align(stack_base() + Hw::Mm::KERNEL_STACK_SIZE);
}


Cpu::Cpu(unsigned                     const  id,
         Board::Address_space_id_allocator  &addr_space_id_alloc,
         Irq::Pool                          &user_irq_pool,
         Cpu_pool                           &cpu_pool,
         Pd                                 &core_pd,
         Board::Global_interrupt_controller &global_irq_ctrl)
:
	_id               { id },
	_pic              { global_irq_ctrl },
	_timer            { *this },
	_idle             { addr_space_id_alloc, user_irq_pool, cpu_pool, *this,
	                    core_pd },
	_scheduler        { _timer, _idle },
	_ipi_irq          { *this },
	_global_work_list { cpu_pool.work_list() }
{
	_arch_init();

	/*
	 * We insert the cpu objects in order into the cpu_pool's list
	 * to ensure that the cpu with the lowest given id is the first
	 * one.
	 */
	Cpu * cpu = cpu_pool._cpus.first();
	while (cpu && cpu->next() && (cpu->next()->id() < _id))
		cpu = cpu->next();
	cpu = (cpu && cpu->id() < _id) ? cpu : nullptr;
	cpu_pool._cpus.insert(this, cpu);
}


/**************
 ** Cpu_pool **
 **************/

template <typename T>
static inline T* cpu_object_by_id(unsigned const id)
{
	using namespace Hw::Mm;
	addr_t base = CPU_LOCAL_MEMORY_AREA_START + id*CPU_LOCAL_MEMORY_SLOT_SIZE;
	return (T*)(base + CPU_LOCAL_MEMORY_SLOT_OBJECT_OFFSET);
}


void
Cpu_pool::
initialize_executing_cpu(Board::Address_space_id_allocator  &addr_space_id_alloc,
                         Irq::Pool                          &user_irq_pool,
                         Pd                                 &core_pd,
                         Board::Global_interrupt_controller &global_irq_ctrl)
{
	unsigned id = Cpu::executing_id();
	Genode::construct_at<Cpu>(cpu_object_by_id<void>(id), id,
	                          addr_space_id_alloc, user_irq_pool,
	                          *this, core_pd, global_irq_ctrl);
}


Cpu & Cpu_pool::cpu(unsigned const id)
{
	return *cpu_object_by_id<Cpu>(id);
}
