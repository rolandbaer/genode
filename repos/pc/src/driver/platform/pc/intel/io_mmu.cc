/*
 * \brief  Intel IOMMU implementation
 * \author Johannes Schlatow
 * \date   2023-08-15
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* local includes */
#include <intel/io_mmu.h>

using namespace Driver;

static void attribute_hex(Genode::Generator &g, char const * name,
                          unsigned long long value)
{
	g.attribute(name, Genode::String<32>(Genode::Hex(value)));
}


template <typename TABLE>
void Intel::Io_mmu::Domain<TABLE>::enable_pci_device(Io_mem_dataspace_capability const,
                                                     Pci::Bdf const &bdf)
{
	Domain_id cur_domain =
		_intel_iommu.root_table().insert_context<TABLE::address_width()>(
			bdf, _translation_table_phys, _domain_id);

	/**
	 * We need to invalidate the context-cache entry for this device and
	 * IOTLB entries for the previously used domain id.
	 *
	 * If the IOMMU caches unresolved requests, we must invalidate those. In
	 * legacy translation mode, these are cached with domain id 0. This is
	 * currently implemented as global invalidation, though.
	 *
	 * Some older architectures also require explicit write-buffer flushing
	 * unless invalidation takes place.
	 */
	if (cur_domain.valid())
		_intel_iommu.invalidator().invalidate_all(cur_domain, Pci::Bdf::rid(bdf));
	else if (_intel_iommu.caching_mode())
		_intel_iommu.invalidator().invalidate_context(Domain_id(), Pci::Bdf::rid(bdf));
	else
		_intel_iommu.flush_write_buffer();
}


template <typename TABLE>
void Intel::Io_mmu::Domain<TABLE>::disable_pci_device(Pci::Bdf const &bdf)
{
	_intel_iommu.root_table().remove_context(bdf, _translation_table_phys);

	/* lookup default mappings and insert instead */
	_intel_iommu.apply_default_mappings(bdf);

	_intel_iommu.invalidator().invalidate_all(_domain_id);
}


template <typename TABLE>
void Intel::Io_mmu::Domain<TABLE>::add_range(Range const &range,
                                             addr_t const paddr,
                                             Dataspace_capability const)
{
	addr_t const             vaddr   { range.start };
	size_t const             size    { range.size };

	Page_flags flags { RW, NO_EXEC, USER, NO_GLOBAL,
	                   RAM, Genode::CACHED };

	_translation_table.insert_translation(vaddr, paddr, size, flags,
	                                      _table_allocator,
	                                      !_intel_iommu.coherent_page_walk(),
	                                      _intel_iommu.supported_page_sizes());

	if (_skip_invalidation)
		return;

	/* only invalidate iotlb if failed requests are cached */
	if (_intel_iommu.caching_mode())
		_intel_iommu.invalidator().invalidate_iotlb(_domain_id);
	else
		_intel_iommu.flush_write_buffer();
}


template <typename TABLE>
void Intel::Io_mmu::Domain<TABLE>::remove_range(Range const &range)
{
	_translation_table.remove_translation(range.start, range.size,
	                                      _table_allocator,
	                                      !_intel_iommu.coherent_page_walk());

	if (!_skip_invalidation)
		_intel_iommu.invalidator().invalidate_iotlb(_domain_id);
}


/* Flush write-buffer if required by hardware */
void Intel::Io_mmu::flush_write_buffer()
{
	if (!read<Capability::Rwbf>())
		return;

	Global_status::access_t  status = read<Global_status>();
	Global_command::access_t cmd    = status;

	/* keep status bits but clear one-shot bits */
	Global_command::Srtp::clear(cmd);
	Global_command::Sirtp::clear(cmd);

	Global_command::Wbf::set(cmd);
	write<Global_command>(cmd);

	/* wait until command completed */
	while (read<Global_status>() != status);
}


Intel::Invalidator & Intel::Io_mmu::invalidator()
{
	if (!read<Global_status::Qies>())
		return *_register_invalidator;
	else
		return *_queued_invalidator;
}


void Intel::Io_mmu::_handle_faults()
{
	if (_fault_irq.constructed())
		_fault_irq->ack_irq();

	handle_faults();
}


void Intel::Io_mmu::handle_faults()
{
	Fault_status::access_t status = read<Fault_status>();

	if (Fault_status::Overflow::get(status))
		error("Fault recording overflow");

	if (Fault_status::Iqe::get(status))
		error("Invalidation queue error: ", Hex(read<Invalidation_queue_error>()));

	if (Fault_status::Ice::get(status))
		error("Invalidation completion error");

	if (Fault_status::Ite::get(status))
		error("Invalidation time-out error");

	/* acknowledge all faults */
	write<Fault_status>(status);

	if (Fault_status::Pending::get(status)) {
		error("Faults records for ", name());
		unsigned num_registers = read<Capability::Nfr>() + 1;
		for (unsigned i = Fault_status::Fri::get(status); ; i = (i + 1) % num_registers) {
			Fault_record_hi::access_t hi = read_fault_record<Fault_record_hi>(i);

			if (!Fault_record_hi::Fault::get(hi))
				break;

			Fault_record_hi::access_t lo = read_fault_record<Fault_record_lo>(i);

			error("Fault: hi=", Hex(hi),
			      ", reason=", Hex(Fault_record_hi::Reason::get(hi)),
			      ", type=",   Hex(Fault_record_hi::Type::get(hi)),
			      ", AT=",     Hex(Fault_record_hi::At::get(hi)),
			      ", EXE=",    Hex(Fault_record_hi::Exe::get(hi)),
			      ", PRIV=",   Hex(Fault_record_hi::Priv::get(hi)),
			      ", PP=",     Hex(Fault_record_hi::Pp::get(hi)),
			      ", Source=", Hex(Fault_record_hi::Source::get(hi)),
			      ", info=",   Hex(Fault_record_lo::Info::get(lo)));


			clear_fault_record(i);
		}
	}
}


bool Intel::Io_mmu::iq_error()
{
	Fault_status::access_t status = read<Fault_status>();
	return Fault_status::Iqe::get(status) ||
	       Fault_status::Ice::get(status) ||
	       Fault_status::Ite::get(status);
}


void Intel::Io_mmu::generate(Generator &g)
{
	g.node("intel", [&] () {
		g.attribute("name", name());

		const bool enabled = (bool)read<Global_status::Enabled>();
		const bool rtps    = (bool)read<Global_status::Rtps>();
		const bool ires    = (bool)read<Global_status::Ires>();
		const bool irtps   = (bool)read<Global_status::Irtps>();
		const bool cfis    = (bool)read<Global_status::Cfis>();

		g.attribute("dma_remapping", enabled && rtps);
		g.attribute("msi_remapping", ires && irtps);
		g.attribute("irq_remapping", ires && irtps && !cfis);

		/* dump registers */
		g.attribute("version", String<16>(read<Version::Major>(), ".",
		                                  read<Version::Minor>()));

		g.node("register", [&] () {
			g.attribute("name", "Capability");
			attribute_hex(g, "value", read<Capability>());
			g.attribute("esrtps", (bool)read<Capability::Esrtps>());
			g.attribute("esirtps", (bool)read<Capability::Esirtps>());
			g.attribute("rwbf",   (bool)read<Capability::Rwbf>());
			g.attribute("nfr",     read<Capability::Nfr>());
			g.attribute("domains", read<Capability::Domains>());
			g.attribute("caching", (bool)read<Capability::Caching_mode>());
		});

		g.node("register", [&] () {
			g.attribute("name", "Extended Capability");
			attribute_hex(g, "value", read<Extended_capability>());
			g.attribute("interrupt_remapping",
			            (bool)read<Extended_capability::Ir>());
			g.attribute("page_walk_coherency",
			            (bool)read<Extended_capability::Page_walk_coherency>());
		});

		g.node("register", [&] () {
			g.attribute("name", "Global Status");
			attribute_hex(g, "value", read<Global_status>());
			g.attribute("qies",    (bool)read<Global_status::Qies>());
			g.attribute("ires",    (bool)read<Global_status::Ires>());
			g.attribute("rtps",    (bool)read<Global_status::Rtps>());
			g.attribute("irtps",   (bool)read<Global_status::Irtps>());
			g.attribute("cfis",    (bool)read<Global_status::Cfis>());
			g.attribute("enabled", (bool)read<Global_status::Enabled>());
		});

		if (!_verbose)
			return;

		g.node("register", [&] () {
			g.attribute("name", "Fault Status");
			attribute_hex(g, "value", read<Fault_status>());
			attribute_hex(g, "fri",   read<Fault_status::Fri>());
			g.attribute("iqe", (bool)read<Fault_status::Iqe>());
			g.attribute("ppf", (bool)read<Fault_status::Pending>());
			g.attribute("pfo", (bool)read<Fault_status::Overflow>());
		});

		g.node("register", [&] () {
			g.attribute("name", "Fault Event Control");
			attribute_hex(g, "value", read<Fault_event_control>());
			g.attribute("mask", (bool)read<Fault_event_control::Mask>());
		});

		if (read<Global_status::Irtps>())
			_irq_table.generate(g);

		if (!read<Global_status::Rtps>())
			return;

		addr_t rt_addr = Root_table_address::Address::masked(read<Root_table_address>());

		g.node("register", [&] () {
			g.attribute("name", "Root Table Address");
			attribute_hex(g, "value", rt_addr);
		});

		if (read<Root_table_address::Mode>() != Root_table_address::Mode::LEGACY) {
			error("Only supporting legacy translation mode");
			return;
		}

		/* dump root table, context table, and page tables */
		_report_helper.with_table<Root_table>(rt_addr,
			[&] (Root_table &root_table) {
				root_table.generate(g, _report_helper);
			});
	});
}


void Intel::Io_mmu::add_default_range(Range const &range, addr_t paddr)
{
	addr_t const             vaddr   { range.start };
	size_t const             size    { range.size };

	Page_flags flags { RW, NO_EXEC, USER, NO_GLOBAL,
	                   RAM, Genode::CACHED };

	try {
		_default_mappings.insert_translation(vaddr, paddr, size, flags,
		                                     supported_page_sizes());
	} catch (...) { /* catch any double insertions */ }
}


void Intel::Io_mmu::default_mappings_complete()
{
	Root_table_address::access_t rtp =
		Root_table_address::Address::masked(_managed_root_table.phys_addr());

	/* skip if already set */
	if (read<Root_table_address>() == rtp)
		return;

	/* insert contexts into managed root table */
	_default_mappings.copy_stage2(_managed_root_table);

	_enable_translation();

	log("enabled IOMMU ", name(), " with default mappings");
}


void Intel::Io_mmu::resume()
{
	_init();
	_enable_translation();
	_enable_irq_remapping();
}


void Intel::Io_mmu::_enable_irq_remapping()
{
	/*
	 * If IRQ remapping has already been enabled during boot, the kernel is
	 * in charge of the remapping. Since there is no way to get the required
	 * unremapped vector for requested MSI, we cannot take over control.
	 */

	if (read<Global_status::Ires>()) {
		warning("IRQ remapping is controlled by kernel for ", name());
		return;
	}

	/* caches must be cleared if Esirtps is not set */
	if (read<Capability::Esirtps>())
		invalidator().invalidate_irq(0, true);

	/* set interrupt remapping table address */
	write<Irq_table_address>(
		Irq_table_address::Size::bits(Irq_table::ENTRIES_LOG2-1) |
		Irq_table_address::Address::masked(_irq_table_phys));

	/* issue set interrupt remapping table pointer command */
	_global_command<Global_command::Sirtp>(1);

	/* disable compatibility format interrupts */
	_global_command<Global_command::Cfi>(0);

	/* enable interrupt remapping */
	_global_command<Global_command::Ire>(1);

	log("enabled interrupt remapping for ", name());

	_remap_irqs = true;
}


void Intel::Io_mmu::_enable_translation()
{
	Root_table_address::access_t rtp =
		Root_table_address::Address::masked(_managed_root_table.phys_addr());

	/* set root table address */
	write<Root_table_address>(rtp);

	/* issue set root table pointer command */
	_global_command<Global_command::Srtp>(1);

	/* caches must be cleared if Esrtps is not set (see 6.6) */
	if (!read<Capability::Esrtps>())
		invalidator().invalidate_all();

	/* enable IOMMU */
	if (!read<Global_status::Enabled>())
		_global_command<Global_command::Enable>(1);
}


void Intel::Io_mmu::_init()
{
	if (read<Global_status::Enabled>()) {
		log("IOMMU has been enabled during boot");

		/* disable queued invalidation interface */
		if (read<Global_status::Qies>())
			_global_command<Global_command::Qie>(false);
	}

	if (read<Extended_capability::Qi>()) {
		/* enable queued invalidation if supported */
		_queued_invalidator.construct(_env, *this, base() + 0x80);
		_global_command<Global_command::Qie>(true);
	} else {
		/* use register-based invalidation interface as fallback */
		addr_t context_reg_base = base() + 0x28;
		addr_t iotlb_reg_base   = base() + 8*_offset<Extended_capability::Iro>();
		_register_invalidator.construct(context_reg_base, iotlb_reg_base, _verbose);
	}

	/* enable fault event interrupts if desired */
	if (_fault_irq.constructed()) {
		Irq_session::Info info = _fault_irq->info();

		if (info.type == Irq_session::Info::INVALID)
			error("Unable to enable fault event interrupts for ", _name);
		else {
			write<Fault_event_address>((Fault_event_address::access_t)info.address);
			write<Fault_event_data>((Fault_event_data::access_t)info.value);
			write<Fault_event_control::Mask>(0);
		}
	}

	/*
	 * We always enable IRQ remapping if its supported by the IOMMU. Note, there
	 * might be the possibility that the ACPI DMAR table says otherwise but
	 * we've never seen such a case yet.
	 */
	if (read<Extended_capability::Ir>())
		_enable_irq_remapping();
}


Intel::Io_mmu::Io_mmu(Env                      &env,
                      Io_mmu_devices           &io_mmu_devices,
                      Device::Name       const &name,
                      Device::Io_mem::Range     range,
                      Context_table_allocator  &table_allocator,
                      unsigned                  irq_number)
: Attached_mmio(env, {(char *)range.start, range.size}),
  Driver::Io_mmu(io_mmu_devices, name),
  _env(env),
  _table_allocator(table_allocator),
  _domain_allocator(_max_domains()-1),
  _managed_root_table(_env, _table_allocator, *this, !coherent_page_walk()),
  _default_mappings(_env, _table_allocator, *this, !coherent_page_walk(),
                    _sagaw_to_levels())
{
	if (_broken_device()) {
		error(name, " reports invalid capability registers. Please disable VT-d/IOMMU.");
		return;
	}

	if (!read<Capability::Sagaw_4_level>() && !read<Capability::Sagaw_3_level>()) {
		error("IOMMU does not support 3- or 4-level page tables");
		return;
	}

	/* enable fault event interrupts (if not already enabled by kernel) */
	if (irq_number && !read<Global_status::Ires>()) {
		_fault_irq.construct(_env, irq_number, 0, Irq_session::TYPE_MSI);

		_fault_irq->sigh(_fault_handler);
		_fault_irq->ack_irq();
	}

	_init();
}
