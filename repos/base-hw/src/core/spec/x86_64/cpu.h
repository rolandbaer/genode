/*
 * \brief  x86_64 CPU driver for core
 * \author Adrian-Ken Rueegsegger
 * \author Martin stein
 * \author Reto Buerki
 * \author Stefan Kalkowski
 * \date   2015-02-06
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__SPEC__X86_64__CPU_H_
#define _CORE__SPEC__X86_64__CPU_H_

/* base includes */
#include <util/mmio.h>
#include <util/register.h>
#include <cpu/cpu_state.h>

/* base-hw internal includes */
#include <hw/spec/x86_64/cpu.h>

/* base internal includes */
#include <base/internal/align_at.h>

/* core includes */
#include <types.h>
#include <spec/x86_64/address_space_id_allocator.h>
#include <hw/spec/x86_64/page_table.h>

namespace Kernel { struct Thread_fault; }


namespace Board {
	using namespace Genode;

	class Address_space_id_allocator;
	class Cpu;
}


class Board::Cpu : public Hw::X86_64_cpu
{
	public:

		/**
		 * Task State Segment (TSS)
		 *
		 * See Intel SDM Vol. 3A, section 7.7
		 */
		struct alignas(8) Tss
		{
			uint32_t reserved0;
			uint64_t rsp[3];       /* pl0-3 stack pointer */
			uint64_t reserved1;
			uint64_t ist[7];       /* irq stack pointer   */
			uint64_t reserved2;

			static void init();
		}  __attribute__((packed)) tss { };


		/**
		 * Interrupt Descriptor Table (IDT)
		 *
		 * See Intel SDM Vol. 3A, section 6.10
		 */
		struct Idt { static void init(); };


		/**
		 * Global Descriptor Table (GDT)
		 * See Intel SDM Vol. 3A, section 3.5.1
		 */
		struct alignas(8) Gdt
		{
			uint64_t null_desc         = 0;
			uint64_t sys_cs_64bit_desc = 0x20980000000000;
			uint64_t sys_ds_64bit_desc = 0x20930000000000;
			uint64_t usr_cs_64bit_desc = 0x20f80000000000;
			uint64_t usr_ds_64bit_desc = 0x20f30000000000;
			uint64_t tss_desc[2];

			void init(addr_t tss_addr);
		} __attribute__((packed)) gdt { };


		struct Fpu_context
		{
			static constexpr size_t SIZE = 512;

			/*
			 * FXSAVE area providing storage for x87 FPU, MMX, XMM,
			 * and MXCSR registers.
			 *
			 * For further details see Intel SDM Vol. 2A,
			 * 'FXSAVE instruction'.
			 */
			char _fxsave_area[SIZE] = { 0 };

			struct Context : Mmio<SIZE>
			{
				struct Fcw   : Register<0, 16>  { };
				struct Mxcsr : Register<24, 32> { };

				using Mmio<SIZE>::Mmio;
			};

			Fpu_context()
			{
				Context init({ _fxsave_area, SIZE });
				init.write<Context::Fcw>(0x37f);    /* mask exceptions SysV ABI */
				init.write<Context::Mxcsr>(0x1f80);
			}
		} __attribute__((packed));


		struct alignas(16) Context : Cpu_state, Fpu_context
		{
			enum Eflags {
				EFLAGS_TF     = 1 << 8,
				EFLAGS_IF_SET = 1 << 9,
				EFLAGS_IOPL_3 = 3 << 12,
			};

			Context(bool privileged);

			void print(Output &output) const;

			void for_each_return_address(Const_byte_range_ptr const &stack,
			                             auto const &fn)
			{
				void **fp = (void**)rbp;
				while (stack.contains(fp) && stack.contains(fp + 1) && fp[1]) {
					fn(fp + 1);
					fp = (void **) fp[0];
				}
			}

			Fpu_context& fpu_context() {
				return static_cast<Fpu_context&>(*this); }
		} __attribute__((packed));


		struct Mmu_context
		{
			addr_t cr3;

			Mmu_context(addr_t page_table_base,
			            Board::Address_space_id_allocator &);
		};

		/**
		 * Return kernel name of the executing CPU
		 */
		static unsigned executing_id();

		bool active(Mmu_context &mmu_context);
		void switch_to(Mmu_context &mmu_context);

		static void mmu_fault(Context &regs, Kernel::Thread_fault &fault);

		static void single_step(Context &regs, bool on);

		/**
		 * Invalidate the whole TLB
		 */
		static void invalidate_tlb() { Cr3::write(Cr3::read()); }

		static void clear_memory_region(addr_t const addr,
		                                size_t const size,
		                                bool changed_cache_properties);
};

#endif /* _CORE__SPEC__X86_64__CPU_H_ */
