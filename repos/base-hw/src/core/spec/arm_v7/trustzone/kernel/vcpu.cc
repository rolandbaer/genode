/*
 * \brief  Kernel backend for virtual machines
 * \author Martin Stein
 * \author Stefan Kalkowski
 * \author Benjamin Lamowski
 * \date   2013-10-30
 */

/*
 * Copyright (C) 2013-2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <kernel/cpu.h>
#include <kernel/vcpu.h>
#include <cpu/vcpu_state_trustzone.h>

using namespace Kernel;


Vcpu::Vcpu(Irq::Pool              &user_irq_pool,
           Cpu                    &cpu,
           Genode::Vcpu_state     &state,
           Kernel::Signal_context &context,
           Identity               &id)
:
	Kernel::Object { *this },
	Cpu_context(cpu, Scheduler::Group_id::BACKGROUND),
	_user_irq_pool(user_irq_pool),
	_state(state),
	_context(context),
	_id(id),
	_vcpu_context(cpu)
{
	/* once constructed, exit with a startup exception */
	pause();
	_state.cpu_exception = Genode::VCPU_EXCEPTION_STARTUP;
	_context.submit(1);
}


Vcpu::~Vcpu() {}


void Vcpu::exception(Genode::Cpu_state&)
{
	switch(_state.cpu_exception) {
	case Genode::Cpu_state::INTERRUPT_REQUEST: [[fallthrough]];
	case Genode::Cpu_state::FAST_INTERRUPT_REQUEST:
		_interrupt(_user_irq_pool);
		return;
	case Genode::Cpu_state::DATA_ABORT:
		_state.dfar = Cpu::Dfar::read();
		[[fallthrough]];
	default:
		pause();
		_context.submit(1);
	}
}


bool secure_irq(unsigned const i);


extern "C" void monitor_mode_enter_normal_world(Genode::Vcpu_state&, void*);


void Vcpu::proceed()
{
	unsigned const irq = _state.irq_injection;
	if (irq) {
		if (_cpu().pic().secure(irq)) {
			Genode::raw("Refuse to inject secure IRQ into VM");
		} else {
			_cpu().pic().trigger(irq);
			_state.irq_injection = 0;
		}
	}

	monitor_mode_enter_normal_world(_state, (void*) _cpu().stack_start());
}


void Vcpu::run()
{
	if (_scheduled != ACTIVE) Cpu_context::_activate();
	_scheduled = ACTIVE;
}


void Vcpu::pause()
{
	if (_cpu().id() != Cpu::executing_id()) {
		Genode::error("vCPU pause called from remote core.");
		return;
	}
	_pause_vcpu();
}
