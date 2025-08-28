/*
 * \brief   Cortex A15 specific MMU context initialization
 * \author  Stefan Kalkowski
 * \date    2017-10-17
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* base-hw core includes */
#include <kernel/thread.h>
#include <spec/arm/cortex_a15_cpu.h>

using Cpu = Board::Cpu;


Cpu::Mmu_context::Mmu_context(addr_t table,
                              Board::Address_space_id_allocator &id_alloc)
:
	_addr_space_id_alloc(id_alloc),
	ttbr0(Ttbr_64bit::Ba::masked((Ttbr_64bit::access_t)table))
{
	Ttbr_64bit::Asid::set(ttbr0, _addr_space_id_alloc.alloc().convert<uint8_t>(
		[&] (addr_t v) { return uint8_t(v); },
		[&] (auto &)   { error("asid allocation failed"); return uint8_t(0); }));
}


Cpu::Mmu_context::~Mmu_context()
{
	/* flush TLB by ASID */
	Cpu::Tlbiasid::write(id());
	_addr_space_id_alloc.free(id());
}


void Cpu::mmu_fault_status(Cpu::Fsr::access_t fsr, Kernel::Thread_fault &fault)
{
	enum {
		FAULT_MASK  = 0b111100,
		TRANSLATION = 0b000100,
		PERMISSION  = 0b001100,
	};

	using Fault = Kernel::Thread_fault;

	switch(fsr & FAULT_MASK) {
	case TRANSLATION: fault.type = Fault::PAGE_MISSING; return;
	case PERMISSION:  fault.type = Fault::EXEC;         return;
	default:          fault.type = Fault::UNKNOWN;
	};
};
