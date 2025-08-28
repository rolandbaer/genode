/*
 * \brief  Parts of the kernel interface that are restricted to core
 * \author Martin stein
 * \author Stefan Kalkowski
 * \date   2014-03-15
 */

/*
 * Copyright (C) 2014-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__KERNEL__CORE_INTERFACE_H_
#define _CORE__KERNEL__CORE_INTERFACE_H_

/* base includes */
#include <cpu/cpu_state.h>

/* base-internal includes */
#include <base/internal/native_utcb.h>

/* base-hw includes */
#include <kernel/interface.h>

namespace Genode { class Native_utcb; }

namespace Kernel {

	class Pd;
	class Thread;
	class Signal_receiver;
	class Signal_context;
	class Vcpu;
	class User_irq;
	using Native_utcb = Genode::Native_utcb;
	using Cpu_state   = Genode::Cpu_state;
	template <typename T> class Core_object_identity;

	/**
	 * Kernel names of the kernel calls
	 */
	constexpr Call_arg call_id_new_thread()             { return 100; }
	constexpr Call_arg call_id_delete_thread()          { return 101; }
	constexpr Call_arg call_id_start_thread()           { return 102; }
	constexpr Call_arg call_id_pause_thread()           { return 103; }
	constexpr Call_arg call_id_resume_thread()          { return 104; }
	constexpr Call_arg call_id_thread_pager()           { return 105; }
	constexpr Call_arg call_id_thread_quota()           { return 106; }
	constexpr Call_arg call_id_invalidate_tlb()         { return 107; }
	constexpr Call_arg call_id_new_pd()                 { return 108; }
	constexpr Call_arg call_id_delete_pd()              { return 109; }
	constexpr Call_arg call_id_new_signal_receiver()    { return 110; }
	constexpr Call_arg call_id_new_signal_context()     { return 111; }
	constexpr Call_arg call_id_delete_signal_context()  { return 112; }
	constexpr Call_arg call_id_delete_signal_receiver() { return 113; }
	constexpr Call_arg call_id_new_vcpu()               { return 114; }
	constexpr Call_arg call_id_delete_vcpu()            { return 117; }
	constexpr Call_arg call_id_new_irq()                { return 118; }
	constexpr Call_arg call_id_delete_irq()             { return 119; }
	constexpr Call_arg call_id_ack_irq()                { return 120; }
	constexpr Call_arg call_id_new_obj()                { return 121; }
	constexpr Call_arg call_id_delete_obj()             { return 122; }
	constexpr Call_arg call_id_new_core_thread()        { return 123; }
	constexpr Call_arg call_id_get_cpu_state()          { return 124; }
	constexpr Call_arg call_id_set_cpu_state()          { return 125; }
	constexpr Call_arg call_id_exception_state()        { return 126; }
	constexpr Call_arg call_id_single_step()            { return 127; }
	constexpr Call_arg call_id_ack_pager_signal()       { return 128; }

	/**
	 * Invalidate TLB entries for the `pd` in region `addr`, `sz`
	 */
	inline void invalidate_tlb(Pd &pd, addr_t const addr,
	                           size_t const sz)
	{
		call(call_id_invalidate_tlb(), (Call_arg)&pd, (Call_arg)addr,
		     (Call_arg)sz);
	}


	/**
	 * Configure the CPU quota of a thread
	 *
	 * \param thread  kernel object of the targeted thread
	 * \param quota   new CPU quota value
	 */
	inline void thread_quota(Kernel::Thread &thread, size_t const quota)
	{
		call(call_id_thread_quota(), (Call_arg)&thread, (Call_arg)quota);
	}


	/**
	 * Pause execution of a thread until 'resume_thread' is called on it
	 *
	 * \param thread  pointer to thread kernel object
	 *
	 * This doesn't affect the state of the thread (IPC, signalling, etc.) but
	 * merely wether the thread is allowed for scheduling or not. The pause
	 * state simply masks the thread state when it comes to scheduling. In
	 * contrast to the 'stopped' thread state, which is described in the
	 * documentation of the 'stop_thread/resume_thread' syscalls, the pause
	 * state doesn't freeze the thread state and the UTCB content of a thread.
	 * However, the register state of a thread doesn't change while paused.
	 * The 'pause' and 'resume' syscalls are both core-restricted and may
	 * target any thread. They are used as back end for the CPU session calls
	 * 'pause' and 'resume'. The 'pause/resume' feature is made for
	 * applications like the GDB monitor that transparently want to stop and
	 * continue the execution of a thread no matter what state the thread is
	 * in.
	 */
	inline void pause_thread(Thread &thread)
	{
		call(call_id_pause_thread(), (Call_arg)&thread);
	}


	/**
	 * End blocking of a paused thread
	 *
	 * \param thread  pointer to thread kernel object
	 */
	inline void resume_thread(Thread &thread)
	{
		call(call_id_resume_thread(), (Call_arg)&thread);
	}


	/**
	 * Start execution of a thread
	 *
	 * \param thread  pointer to thread kernel object
	 * \param cpu_id  kernel name of the targeted CPU
	 * \param pd      pointer to pd kernel object
	 * \param utcb    core local pointer to userland thread-context
	 *
	 * \retval   0  suceeded
	 * \retval !=0  failed
	 */
	inline int start_thread(Thread &thread, Pd &pd, Native_utcb &utcb)
	{
		return (int)call(call_id_start_thread(), (Call_arg)&thread,
		                 (Call_arg)&pd, (Call_arg)&utcb);
	}


	/**
	 * Set or unset the handler of an event that can be triggered by a thread
	 *
	 * \param thread             reference to thread kernel object
	 * \param pager              reference to pager kernel object
	 * \param signal_context_id  capability id of the page-fault handler
	 */
	inline void thread_pager(Thread &thread,
	                         Thread &pager,
	                         capid_t const signal_context_id)
	{
		call(call_id_thread_pager(), (Call_arg)&thread, (Call_arg)&pager,
		     signal_context_id);
	}


	/**
	 * Acknowledge interrupt
	 *
	 * \param irq  pointer to interrupt kernel object
	 */
	inline void ack_irq(User_irq &irq)
	{
		call(call_id_ack_irq(), (Call_arg) &irq);
	}


	/**
	 * Get CPU state
	 *
	 * \param thread        pointer to thread kernel object
	 * \param thread_state  pointer to result CPU state object
	 */
	inline void get_cpu_state(Thread &thread, Cpu_state &cpu_state)
	{
		call(call_id_get_cpu_state(), (Call_arg)&thread, (Call_arg)&cpu_state);
	}


	/**
	 * Set CPU state
	 *
	 * \param thread        pointer to thread kernel object
	 * \param thread_state  pointer to CPU state object
	 */
	inline void set_cpu_state(Thread &thread, Cpu_state &cpu_state)
	{
		call(call_id_set_cpu_state(), (Call_arg)&thread, (Call_arg)&cpu_state);
	}


	/**
	 * Enable/disable single-stepping
	 *
	 * \param thread  pointer to thread kernel object
	 * \param on      enable or disable
	 */
	inline void single_step(Thread &thread, bool &on)
	{
		call(call_id_single_step(), (Call_arg)&thread, (Call_arg)&on);
	}

	/**
	 * Acknowledge a signal transmitted to a pager
	 *
	 * \param context   signal context to acknowledge
	 * \param thread    reference to faulting thread kernel object
	 * \param resolved  whether fault got resolved
	 */
	inline void ack_pager_signal(capid_t const context, Thread &thread, bool resolved)
	{
		call(call_id_ack_pager_signal(), context, (Call_arg)&thread, resolved);
	}
}

#endif /* _CORE__KERNEL__CORE_INTERFACE_H_ */
