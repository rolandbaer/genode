/*
 * \brief   Platform implementations specific for x86_64
 * \author  Reto Buerki
 * \author  Stefan Kalkowski
 * \author  Alexander Boettcher
 * \date    2015-05-04
 */

/*
 * Copyright (C) 2015-2022 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <bios_data_area.h>
#include <platform.h>
#include <multiboot.h>
#include <multiboot2.h>
#include <port_io.h>

#include <hw/memory_consts.h>
#include <hw/spec/x86_64/acpi.h>
#include <hw/spec/x86_64/apic.h>
#include <hw/spec/x86_64/x86_64.h>

using namespace Genode;


/* contains Multiboot MAGIC value (either version 1 or 2) */
extern "C" Genode::addr_t __initial_ax;

/* contains physical pointer to multiboot */
extern "C" Genode::addr_t __initial_bx;

/* pointer to stack base */
extern "C" Genode::addr_t bootstrap_stack;

/* number of booted CPUs */
extern "C" Genode::addr_t volatile __cpus_booted;

/* stack size per CPU */
extern "C" Genode::addr_t const bootstrap_stack_size;


/* hardcoded physical page or AP CPUs boot code */
enum { AP_BOOT_CODE_PAGE = 0x1000 };

extern "C" void * _start;
extern "C" void * _ap;


static Hw::Acpi_rsdp search_rsdp(addr_t area, addr_t area_size)
{
	if (area && area_size && area < area + area_size) {
		for (addr_t addr = 0; addr + sizeof(Hw::Acpi_rsdp) <= area_size;
		     addr += sizeof(Hw::Acpi_rsdp::signature))
		{
			Hw::Acpi_rsdp * rsdp = reinterpret_cast<Hw::Acpi_rsdp *>(area + addr);
			if (rsdp->valid())
				return *rsdp;
		}
	}

	Hw::Acpi_rsdp invalid { };
	return invalid;
}


static uint32_t calibrate_tsc_frequency(addr_t fadt_addr)
{
	uint32_t const default_freq = 2'400'000;

	if (!fadt_addr) {
		warning("FADT not found, returning fixed TSC frequency of ", default_freq, "kHz");
		return default_freq;
	}

	uint32_t const sleep_ms = 10;

	Hw::Acpi_fadt fadt(reinterpret_cast<Hw::Acpi_generic *>(fadt_addr));

	uint32_t const freq = fadt.calibrate_freq_khz(sleep_ms, []() { return Hw::Tsc::rdtsc(); });

	if (!freq) {
		warning("Unable to calibrate TSC, returning fixed TSC frequency of ", default_freq, "kHz");
		return default_freq;
	}

	return freq;
}


static Hw::Local_apic::Calibration calibrate_lapic_frequency(addr_t fadt_addr)
{
	uint32_t const default_freq = TIMER_MIN_TICKS_PER_MS;

	if (!fadt_addr) {
		warning("FADT not found, setting minimum Local APIC frequency of ", default_freq, "kHz");
		return { default_freq, 1 };
	}

	uint32_t const sleep_ms = 10;

	Hw::Acpi_fadt fadt(reinterpret_cast<Hw::Acpi_generic *>(fadt_addr));

	Hw::Local_apic lapic(Hw::Cpu_memory_map::lapic_phys_base());

	auto const result =
		lapic.calibrate_divider([&] {
			return fadt.calibrate_freq_khz(sleep_ms, [&] {
				return lapic.read<Hw::Local_apic::Tmr_current>(); }, true); });

	if (!result.freq_khz) {
		warning("FADT not found, setting minimum Local APIC frequency of ", default_freq, "kHz");
		return { default_freq, 1 };
	}

	return result;
}


static void disable_pit()
{
	using Hw::outb;

	enum {
		/* PIT constants */
		PIT_CH0_DATA   = 0x40,
		PIT_MODE       = 0x43,
	};

	/*
	 * Disable PIT timer channel. This is necessary since BIOS sets up
	 * channel 0 to fire periodically.
	 */
	outb(PIT_MODE, 0x30);
	outb(PIT_CH0_DATA, 0);
	outb(PIT_CH0_DATA, 0);
}


/*
 * Enable dispatch serializing lfence instruction on AMD processors
 *
 * See Software techniques for managing speculation on AMD processors
 *     Revision 5.09.23
 *     Mitigation G-2
 */
static void amd_enable_serializing_lfence()
{
	using Cpu = Hw::X86_64_cpu;

	if (Hw::Vendor::get_vendor_id() != Hw::Vendor::Vendor_id::AMD)
		return;

	unsigned const family = Hw::Vendor::get_family();

	/*
	 * In family 0Fh and 11h, lfence is always dispatch serializing and
	 * "AMD plans support for this MSR and access to this bit for all future
	 * processors." from family 14h on.
	 */
	if ((family == 0x10) || (family == 0x12) || (family >= 0x14)) {
		Cpu::Amd_lfence::access_t amd_lfence = Cpu::Amd_lfence::read();
		Cpu::Amd_lfence::Enable_dispatch_serializing::set(amd_lfence);
		Cpu::Amd_lfence::write(amd_lfence);
	}
}


static inline void memory_add_region(addr_t base, addr_t size,
                                     Hw::Memory_region_array &early,
                                     Hw::Memory_region_array &late)
{
	using namespace Hw;

	static constexpr size_t initial_map_max = 1024 * 1024 * 1024;

	/*
	 * Exclude first physical page, so that it will become part of the
	 * MMIO allocator. The framebuffer requests this page as MMIO.
	 */
	if (base == 0 && size >= get_page_size()) {
		base  = get_page_size();
		size -= get_page_size();
	}

	/* exclude AP boot code page from normal RAM allocator */
	if (base <= AP_BOOT_CODE_PAGE && AP_BOOT_CODE_PAGE < base + size) {
		if (AP_BOOT_CODE_PAGE - base)
			early.add(Memory_region { base, AP_BOOT_CODE_PAGE - base });

		size -= AP_BOOT_CODE_PAGE - base;
		size -= (get_page_size() > size) ? size : get_page_size();
		base  = AP_BOOT_CODE_PAGE + get_page_size();
	}

	/* skip partial 4k pages (seen with Qemu with ahci model enabled) */
	if (!aligned(base, 12)) {
		auto new_base = align_addr(base, 12);
		size -= (new_base - base > size) ? size : new_base - base;
		base  = new_base;
	}

	/* remove partial 4k pages */
	if (!aligned(size, 12))
		size -= size & 0xffful;

	if (!size)
		return;

	if (base >= initial_map_max) {
		late.add(Memory_region { base, size });
		return;
	}

	if (base + size <= initial_map_max) {
		early.add(Memory_region { base, size });
		return;
	}

	size_t low_size = initial_map_max - base;
	early.add(Memory_region { base, low_size });
	late.add(Memory_region { initial_map_max, size - low_size });
}


static inline void parse_multi_boot_1(::Board::Boot_info      &info,
                                      Hw::Memory_region_array &early,
                                      Hw::Memory_region_array &late)
{
	for (unsigned i = 0; true; i++) {
		using Mmap = Multiboot_info::Mmap;

		Mmap v(Multiboot_info(__initial_bx).phys_ram_mmap_base(i));
		if (!v.base()) break;

		Mmap::Addr::access_t   base = v.read<Mmap::Addr>();
		Mmap::Length::access_t size = v.read<Mmap::Length>();

		memory_add_region(base, size, early, late);
	}

	/* search ACPI RSDP pointer at known places */

	/* BIOS range to scan for */
	enum { BIOS_BASE = 0xe0000, BIOS_SIZE = 0x20000 };
	info.acpi_rsdp = search_rsdp(BIOS_BASE, BIOS_SIZE);

	if (!info.acpi_rsdp.valid()) {
		/* page 0 is remapped to 2M - 4k - see crt_translation table */
		addr_t const bios_addr = 2 * 1024 * 1024 - 4096;

		/* search EBDA (BIOS addr + 0x40e) */
		addr_t ebda_phys = (*reinterpret_cast<uint16_t *>(bios_addr + 0x40e)) << 4;
		if (ebda_phys < 0x1000)
			ebda_phys = bios_addr;

		info.acpi_rsdp = search_rsdp(ebda_phys, 0x1000 /* EBDA size */);
	}
}


static inline void parse_multi_boot_2(::Board::Boot_info      &info,
                                      Hw::Memory_region_array &early,
                                      Hw::Memory_region_array &late)
{
	Multiboot2_info mbi2(__initial_bx);

	mbi2.for_each_tag([&] (Multiboot2_info::Memory const &m) {
		uint32_t const type = m.read<Multiboot2_info::Memory::Type>();

		if (type != Multiboot2_info::Memory::Type::MEMORY)
			return;

		uint64_t const base = m.read<Multiboot2_info::Memory::Addr>();
		uint64_t const size = m.read<Multiboot2_info::Memory::Size>();

		memory_add_region(base, size, early, late);
	},
	[&] (Hw::Acpi_rsdp const &rsdp_v1) {
		/* only use ACPI RSDP v1 if nothing available/valid by now */
		if (!info.acpi_rsdp.valid())
			info.acpi_rsdp = rsdp_v1;
	},
	[&] (Hw::Acpi_rsdp const &rsdp_v2) {
		/* prefer v2 ever, override stored previous rsdp v1 potentially */
		info.acpi_rsdp = rsdp_v2;
	},
	[&] (Hw::Framebuffer const &fb) {
		info.framebuffer = fb;
	},
	[&] (uint64_t const efi_sys_tab) {
		info.efi_system_table = efi_sys_tab;
	});
}


static inline void acpi_system_description_table(Hw::Acpi_rsdp &acpi_rsdp,
                                                 auto const &per_table)
{
	if (!acpi_rsdp.valid())
		return;

	uint64_t const table_addr = acpi_rsdp.xsdt ? acpi_rsdp.xsdt : acpi_rsdp.rsdt;
	if (!table_addr)
		return;

	auto lambda = [&] (auto paddr_table) {
		addr_t const table_virt_addr = paddr_table;
		per_table(*reinterpret_cast<Hw::Acpi_generic *>(table_virt_addr),
		                                                paddr_table);
	};

	Hw::Acpi_generic &table = *reinterpret_cast<Hw::Acpi_generic*>(table_addr);
	if (!memcmp(table.signature, "RSDT", 4)) {
		Hw::for_each_rsdt_entry(table, lambda);
	} else if (!memcmp(table.signature, "XSDT", 4)) {
		Hw::for_each_xsdt_entry(table, lambda);
	}
}


static inline void for_each_cpu(Hw::Acpi_rsdp &acpi_rsdp,
                                auto const &per_cpu_fn)
{
	auto lambda = [&] (Hw::Acpi_generic &table, uint64_t) {
		Hw::for_each_apic_struct(table,[&](Hw::Apic_madt const *e) {
			if (e->type != Hw::Apic_madt::LAPIC)
				return;

			/* check if APIC is enabled in hardware */
			Hw::Apic_madt::Lapic lapic(e);
			if (lapic.valid()) per_cpu_fn(lapic.id());
		});
	};
	acpi_system_description_table(acpi_rsdp, lambda);
}


Bootstrap::Platform::Board::Board()
:
	core_mmio(Memory_region { 0, 0x1000 },
	          Memory_region { Hw::Cpu_memory_map::lapic_phys_base(), 0x1000 },
	          Memory_region { Hw::Cpu_memory_map::MMIO_IOAPIC_BASE,
	                          Hw::Cpu_memory_map::MMIO_IOAPIC_SIZE },
	          Memory_region { __initial_bx & ~0xFFFUL,
	                          get_page_size() })
{
	switch (__initial_ax) {
	case Multiboot_info::MAGIC:
		parse_multi_boot_1(info, early_ram_regions, late_ram_regions);
		break;
	case Multiboot2_info::MAGIC:
		parse_multi_boot_2(info, early_ram_regions, late_ram_regions);
		break;
	default:
		error("invalid multiboot magic value: ", Hex(__initial_ax));
	};

	/* scan ACPI tables for FADT */
	auto lambda = [&] (Hw::Acpi_generic &table, uint64_t paddr) {

		/* its signature is called FACP */
		if (memcmp(table.signature, "FACP", 4))
			return;

		info.acpi_fadt = addr_t(&table);

		/* show firmware that we're an ACPI-aware OS */
		Hw::Acpi_fadt fadt(&table);
		fadt.takeover_acpi();

		Hw::Acpi_facs facs(fadt.facs());
		facs.wakeup_vector(AP_BOOT_CODE_PAGE);

		auto mem_aligned = paddr & _align_mask(12);
		auto mem_size    = align_addr(paddr + table.size, 12) - mem_aligned;
		core_mmio.add({ mem_aligned, mem_size });
	};
	acpi_system_description_table(info.acpi_rsdp, lambda);

	/* get the actual number of cpus */
	static constexpr unsigned MAX_CPUS =
		Hw::Mm::CPU_LOCAL_MEMORY_AREA_SIZE /
		Hw::Mm::CPU_LOCAL_MEMORY_SLOT_SIZE;
	cpus = 0;
	for_each_cpu(info.acpi_rsdp, [this] (unsigned) { cpus++; });

	if (!cpus || cpus > MAX_CPUS) {
		Genode::warning("CPU count is unsupported ", cpus, "/", MAX_CPUS,
		                info.acpi_rsdp.valid() ? " - invalid or missing RSDT/XSDT"
		                                       : " - invalid RSDP");
		cpus = !cpus ? 1 : MAX_CPUS;
	}

	/*
	 * Enable serializing lfence on supported AMD processors
	 *
	 * For APs this will be set up later, but we need it already to obtain
	 * the most acurate results when calibrating the TSC frequency.
	 */
	amd_enable_serializing_lfence();

	auto r = calibrate_lapic_frequency(info.acpi_fadt);
	info.lapic_freq_khz = r.freq_khz;
	info.lapic_div      = r.div;
	info.tsc_freq_khz   = calibrate_tsc_frequency(info.acpi_fadt);

	disable_pit();

	/* copy 16 bit boot code for AP CPUs and for ACPI resume */
	addr_t ap_code_size = (addr_t)&_start - (addr_t)&_ap;
	memcpy((void *)AP_BOOT_CODE_PAGE, &_ap, ap_code_size);
}


static inline
void ipi_to_all(Hw::Local_apic &lapic, unsigned const boot_frame,
                Hw::Local_apic::Icr_low::Delivery_mode::Mode const mode)
{
	using Icr_low = Hw::Local_apic::Icr_low;

	/* wait until ready */
	while (lapic.read<Icr_low::Delivery_status>())
		asm volatile ("pause":::"memory");

	unsigned const apic_cpu_id = 0; /* unused for IPI to all */

	Icr_low::access_t icr_low = 0;
	Icr_low::Vector::set(icr_low, boot_frame);
	Icr_low::Delivery_mode::set(icr_low, mode);
	Icr_low::Level_assert::set(icr_low);
	Icr_low::Level_assert::set(icr_low);
	Icr_low::Dest_shorthand::set(icr_low, Icr_low::Dest_shorthand::ALL_OTHERS);

	/* program */
	lapic.write<Hw::Local_apic::Icr_high::Destination>(apic_cpu_id);
	lapic.write<Icr_low>(icr_low);
}


unsigned Bootstrap::Platform::enable_mmu()
{
	using ::Board::Cpu;

	/* enable PAT if available */
	Cpu::Cpuid_1_edx::access_t cpuid1 = Cpu::Cpuid_1_edx::read();
	if (Cpu::Cpuid_1_edx::Pat::get(cpuid1)) {
		Cpu::IA32_pat::access_t pat = Cpu::IA32_pat::read();
		if (Cpu::IA32_pat::Pa1::get(pat) != Cpu::IA32_pat::Pa1::WRITE_COMBINING) {
			Cpu::IA32_pat::Pa1::set(pat, Cpu::IA32_pat::Pa1::WRITE_COMBINING);
			Cpu::IA32_pat::write(pat);
		}
	}

	Cpu::Cr3::write(Cpu::Cr3::Pdb::masked((addr_t)core_pd->table_base));

	auto const cpu_id =
		Cpu::Cpuid_1_ebx::Apic_id::get(Cpu::Cpuid_1_ebx::read());

	/* we like to use local APIC */
	Cpu::IA32_apic_base::access_t lapic_msr = Cpu::IA32_apic_base::read();
	Cpu::IA32_apic_base::Lapic::set(lapic_msr);
	Cpu::IA32_apic_base::write(lapic_msr);

	Hw::Local_apic lapic(board.core_mmio.virt_addr(Hw::Cpu_memory_map::lapic_phys_base()));

	/* enable local APIC if required */
	if (!lapic.read<Hw::Local_apic::Svr::APIC_enable>())
		lapic.write<Hw::Local_apic::Svr::APIC_enable>(true);

	/* reset assembly counter (crt0.s) by last booted CPU, required for resume */
	if (__cpus_booted >= board.cpus)
		__cpus_booted = 0;

	/* skip wakeup IPI for non SMP setups */
	if (board.cpus <= 1)
		return (unsigned)cpu_id;

	if (!Cpu::IA32_apic_base::Bsp::get(lapic_msr)) {
		/* AP - done */
		/* enable serializing lfence on supported AMD processors. */
		amd_enable_serializing_lfence();
		return (unsigned)cpu_id;
	}

	/* BSP - we're primary CPU - wake now all other CPUs */

	/* see Intel Multiprocessor documentation - we need to do INIT-SIPI-SIPI */
	ipi_to_all(lapic, 0 /* unused */,
	           Hw::Local_apic::Icr_low::Delivery_mode::INIT);
	/* wait 10  ms - debates ongoing whether this is still required */
	ipi_to_all(lapic, AP_BOOT_CODE_PAGE >> 12,
	           Hw::Local_apic::Icr_low::Delivery_mode::SIPI);
	/* wait 200 us - debates ongoing whether this is still required */
	/* debates ongoing whether the second SIPI is still required */
	ipi_to_all(lapic, AP_BOOT_CODE_PAGE >> 12,
	           Hw::Local_apic::Icr_low::Delivery_mode::SIPI);

	return (unsigned)cpu_id;
}


addr_t Bios_data_area::_mmio_base_virt() { return 0x1ff000; }


Board::Serial::Serial(addr_t, size_t, unsigned baudrate)
:
	X86_uart(Bios_data_area::singleton()->serial_port(), 0, baudrate)
{ }


unsigned Bootstrap::Platform::_prepare_cpu_memory_area()
{
	for_each_cpu(board.info.acpi_rsdp, [this] (unsigned id) {
		_prepare_cpu_memory_area(id); });
	return board.cpus;
}
