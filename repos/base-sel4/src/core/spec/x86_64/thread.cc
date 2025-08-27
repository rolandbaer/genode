/*
 * \brief  Utilities for thread creation on seL4
 * \author Norman Feske
 * \date   2015-05-12
 *
 * This file is used by both the core-specific implementation of the Thread API
 * and the platform-thread implementation for managing threads outside of core.
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* base includes */
#include <base/internal/native_utcb.h>
#include <base/thread_state.h>

/* core includes */
#include <thread_sel4.h>
#include <platform_thread.h>

using namespace Core;


bool Core::start_sel4_thread(Cap_sel tcb_sel, addr_t ip, addr_t sp,
                             unsigned cpu, addr_t const virt_utcb)
{
	/* set register values for the instruction pointer and stack pointer */
	seL4_UserContext regs;
	memset(&regs, 0, sizeof(regs));
	size_t const num_regs = sizeof(regs)/sizeof(seL4_Word);

	regs.rip = ip;
	regs.rsp = sp;

	auto ret = seL4_TCB_WriteRegisters(tcb_sel.value(), false, 0,
	                                   num_regs, &regs);
	if (ret != seL4_NoError)
		return false;

	if (!affinity_sel4_thread(tcb_sel, cpu))
		return false;

	/*
	 * Set tls pointer to location, where ipcbuffer address is stored, so
	 * that it can be used by register fs (seL4_GetIPCBuffer())
	 */
	ret = seL4_TCB_SetTLSBase(tcb_sel.value(),
	                          virt_utcb + Native_utcb::tls_ipcbuffer_offset);
	if (ret != seL4_NoError)
		return false;

	return seL4_TCB_Resume(tcb_sel.value()) == seL4_NoError;
}


bool Core::affinity_sel4_thread(Cap_sel const &tcb_sel, unsigned cpu)
{
	return seL4_TCB_SetAffinity(tcb_sel.value(), cpu) == seL4_NoError;
}


Thread_state Platform_thread::state()
{
	seL4_TCB   const thread         = _info.tcb_sel.value();
	seL4_Bool  const suspend_source = false;
	seL4_Uint8 const arch_flags     = 0;
	seL4_UserContext registers;
	seL4_Word  const register_count = sizeof(registers) / sizeof(registers.rip);

	long const ret = seL4_TCB_ReadRegisters(thread, suspend_source, arch_flags,
	                                        register_count, &registers);
	if (ret != seL4_NoError)
		return { .state = Thread_state::State::UNAVAILABLE, .cpu = { } };

	Thread_state state { };

	state.cpu.ip     = registers.rip;
	state.cpu.sp     = registers.rsp;
	state.cpu.rdi    = registers.rdi;
	state.cpu.rsi    = registers.rsi;
	state.cpu.rbp    = registers.rbp;
	state.cpu.rbx    = registers.rbx;
	state.cpu.rdx    = registers.rdx;
	state.cpu.rcx    = registers.rcx;
	state.cpu.rax    = registers.rax;
	state.cpu.r8     = registers.r8;
	state.cpu.r9     = registers.r9;
	state.cpu.r10    = registers.r10;
	state.cpu.r11    = registers.r11;
	state.cpu.r12    = registers.r12;
	state.cpu.r13    = registers.r13;
	state.cpu.r14    = registers.r14;
	state.cpu.r15    = registers.r15;
	state.cpu.eflags = registers.rflags;
	state.cpu.trapno = 0; /* XXX detect/track if in exception and report here */
	/* registers.tls_base unused */

	return state;
}
