/*
 *
 * \brief  Board with PC virtualization support
 * \author Benjamin Lamowski
 * \date   2022-10-14
 */

/*
 * Copyright (C) 2022-2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__SPEC__PC__VIRTUALIZATION__BOARD_H_
#define _CORE__SPEC__PC__VIRTUALIZATION__BOARD_H_

#include <session/session.h>

#include <cpu/vcpu_state_virtualization.h>
#include <hw/spec/x86_64/x86_64.h>
#include <spec/x86_64/svm.h>
#include <spec/x86_64/vmx.h>

using Genode::addr_t;
using Genode::uint64_t;

namespace Board {
	struct Vcpu_context;
	using Vcpu_data = Genode::Vcpu_data;
	using Vcpu_state = Genode::Vcpu_state;

	enum Platform_exitcodes : uint64_t {
		EXIT_NPF     = 0xfc,
		EXIT_PAUSED  = 0xff,
	};

	enum Custom_trapnos : uint64_t {
		TRAP_VMEXIT = 256,
	};
};


struct Board::Vcpu_context
{
	enum class Init_state {
		CREATED,
		STARTED
	};

	Vcpu_context(unsigned id, Vcpu_data &vcpu_data);
	void initialize(Kernel::Cpu &cpu, addr_t table_phys_addr);
	void load(Vcpu_state &state);
	void store(Vcpu_state &state);

	Genode::Align_at<Board::Cpu::Context> regs;

	Virt_interface                      &virt;

	uint64_t tsc_aux_host = 0U;
	uint64_t tsc_aux_guest = 0U;
	uint64_t exit_reason = EXIT_PAUSED;

	Init_state init_state { Init_state::CREATED };


	static Virt_interface &detect_virtualization(Vcpu_data &vcpu_data,
	                                             unsigned   id)
	{
		if (Hw::Virtualization_support::has_svm())
			return *Genode::construct_at<Vmcb>(
				vcpu_data.virt_area,
				vcpu_data,
				id);
		else if (Hw::Virtualization_support::has_vmx()) {
			return *Genode::construct_at<Vmcs>(
				vcpu_data.virt_area,
				vcpu_data);
		} else {
			Genode::error( "No virtualization support detected.");
			throw Core::Service_denied();
		}
	}
};

#endif /* _CORE__SPEC__PC__VIRTUALIZATION__BOARD_H_ */
