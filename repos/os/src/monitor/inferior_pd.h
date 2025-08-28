/*
 * \brief  Inferior is a monitored child PD
 * \author Norman Feske
 * \date   2023-05-08
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INFERIOR_PD_H_
#define _INFERIOR_PD_H_

/* Genode includes */
#include <base/attached_dataspace.h>
#include <os/session_policy.h>

/* local includes */
#include <monitored_pd.h>

namespace Monitor { struct Inferior_pd; }


struct Monitor::Inferior_pd : Monitored_pd_session
{
	Inferiors::Element _inferiors_elem;

	Threads _threads { };

	Threads::Id _last_thread_id { };

	Io_signal_handler<Inferior_pd> _page_fault_handler;

	Env::Local_rm &_local_rm;  /* for wiping RAM dataspaces on free */
	Allocator     &_alloc;     /* used for allocating 'Ram_ds' objects */
	Ram_allocator &_wx_ram;    /* RAM used for writeable text segments */

	Monitored_region_map _address_space {
		_ep, _real.call<Pd_session::Rpc_address_space>(), "address space",
		_alloc };

	Monitored_region_map _stack_area {
		_ep, _real.call<Pd_session::Rpc_stack_area>(), "stack area", _alloc };

	Monitored_region_map _linker_area {
		_ep, _real.call<Pd_session::Rpc_linker_area>(), "linker area", _alloc };

	struct Policy
	{
		bool wait;  /* wait for GDB continue command */
		bool stop;  /* stop execution when GDB connects */
		bool wx;    /* make text segments writeable */

		static Policy from_node(Node const &policy)
		{
			return { .wait = policy.attribute_value("wait", false),
			         .stop = policy.attribute_value("stop", true),
			         .wx   = policy.attribute_value("wx",   false) };
		}

		static Policy default_policy() { return from_node(Node()); }
	};

	Policy _policy = Policy::default_policy();

	unsigned _page_fault_count = 0;

	void _handle_page_fault()
	{
		bool thread_found = false;

		for_each_thread([&] (Monitored_thread &thread) {

			if (thread.stop_state != Monitored_thread::Stop_state::RUNNING)
				return;

			Thread_state thread_state = thread.state();
			if (thread_state.state == Thread_state::State::PAGE_FAULT) {
				thread.handle_page_fault();
				thread_found = true;
			}
		});

		if (!thread_found) {
			/*
			 * Fault caused by memory accessor
			 *
			 * If both an inferior thread and the memory accessor
			 * caused a page fault, this is not detected here and
			 * the watchdog timeout of the memory accessor will
			 * trigger instead.
			 */
			_page_fault_count++;
		}
	}

	/**
	 * Keep track of allocated RAM dataspaces for wiping when freed
	 */
	struct Ram_ds : Id_space<Ram_ds>::Element
	{
		static Id_space<Ram_ds>::Id id(Dataspace_capability const &cap)
		{
			return { (unsigned long)cap.local_name() };
		}

		Ram_ds(Id_space<Ram_ds> &id_space, Dataspace_capability cap)
		:
			Id_space<Ram_ds>::Element(*this, id_space, id(cap)), cap(cap)
		{ }

		Dataspace_capability cap;
	};

	Id_space<Ram_ds> _ram_dataspaces { };

	void _wipe_ram_ds(Ram_ds &ram_ds)
	{
		{
			Attached_dataspace ds(_local_rm, ram_ds.cap);
			memset(ds.local_addr<void>(), 0, ds.size());
		}
		destroy(_alloc, &ram_ds);
	}


	Inferior_pd(Entrypoint &ep, Capability<Pd_session> real, Name const &name,
	            Inferiors &inferiors, Inferiors::Id id, Env::Local_rm &local_rm,
	            Allocator &alloc, Ram_allocator &wx_ram)
	:
		Monitored_pd_session(ep, real, name),
		_inferiors_elem(*this, inferiors, id),
		_page_fault_handler(ep, *this, &Inferior_pd::_handle_page_fault),
		_local_rm(local_rm), _alloc(alloc), _wx_ram(wx_ram)
	{
		_address_space._real.call<Region_map::Rpc_fault_handler>(_page_fault_handler);
		_stack_area   ._real.call<Region_map::Rpc_fault_handler>(_page_fault_handler);
		_linker_area  ._real.call<Region_map::Rpc_fault_handler>(_page_fault_handler);
	}

	~Inferior_pd()
	{
		while (_ram_dataspaces.apply_any<Ram_ds &>([&] (Ram_ds &ram_ds) {
			_wipe_ram_ds(ram_ds); }));

		while (_threads.apply_any<Monitored_thread &>([&] (Monitored_thread &thread) {
			destroy(_alloc, &thread); }));
	}

	void apply_monitor_config(Node const &monitor)
	{
		with_matching_policy(_name, monitor,
			[&] (Node const &policy) { _policy = Policy::from_node(policy); },
			[&]                      { _policy = Policy::default_policy(); });

		if (_policy.wx) {
			_address_space.writeable_text_segments(_alloc, _wx_ram, _local_rm);
			_linker_area  .writeable_text_segments(_alloc, _wx_ram, _local_rm);
		}
	}

	long unsigned id() const { return _inferiors_elem.id().value; }

	unsigned page_fault_count() const { return _page_fault_count; }

	Threads::Id alloc_thread_id()
	{
		_last_thread_id.value++;
		return _last_thread_id;
	}

	void for_each_thread(auto const &fn) const
	{
		_threads.for_each<Monitored_thread &>(fn);
	}

	static void with_inferior_pd(Entrypoint &ep, Capability<Pd_session> pd_cap,
	                             auto const &monitored_fn, auto const &direct_fn)
	{
		with_monitored<Inferior_pd>(ep, pd_cap, monitored_fn, direct_fn);
	}


	/**************************
	 ** Pd_session interface **
	 **************************/

	void assign_parent(Capability<Parent> parent) override {
		_real.call<Rpc_assign_parent>(parent); }

	bool assign_pci(addr_t pci_config_memory_address, uint16_t bdf) override {
		return _real.call<Rpc_assign_pci>(pci_config_memory_address, bdf); }

	Map_result map(Virt_range range) override {
		return _real.call<Rpc_map>(range); }

	Signal_source_result signal_source() override {
		return _real.call<Rpc_signal_source>(); }

	void free_signal_source(Capability<Signal_source> cap) override {
		_real.call<Rpc_free_signal_source>(cap); }

	Alloc_context_result alloc_context(Capability<Signal_source> source,
	                                   Imprint imprint) override {
		return _real.call<Rpc_alloc_context>(source, imprint); }

	void free_context(Signal_context_capability cap) override {
		_real.call<Rpc_free_context>(cap); }

	void submit(Signal_context_capability receiver, unsigned cnt = 1) override {
		_real.call<Rpc_submit>(receiver, cnt); }

	Alloc_rpc_cap_result alloc_rpc_cap(Native_capability ep) override {
		return _real.call<Rpc_alloc_rpc_cap>(ep); }

	void free_rpc_cap(Native_capability cap) override {
		_real.call<Rpc_free_rpc_cap>(cap); }

	Capability<Region_map> address_space() override { return _address_space.cap(); }

	Capability<Region_map> stack_area()    override { return _stack_area.cap(); }

	Capability<Region_map> linker_area()   override { return _linker_area.cap(); }

	Cap_quota cap_quota() const override {
		return _real.call<Rpc_cap_quota>(); }

	Cap_quota used_caps() const override {
		return _real.call<Rpc_used_caps>(); }

	Alloc_ram_result alloc_ram(size_t size, Cache cache = CACHED) override
	{
		return _real.call<Rpc_alloc_ram>(size, cache).convert<Alloc_ram_result>(
			[&] (Ram_dataspace_capability cap) -> Alloc_ram_result {
				new (_alloc) Ram_ds(_ram_dataspaces, cap);
				return cap;
			},
			[&] (Alloc_error e) -> Alloc_ram_result { return e; });
	}

	void free_ram(Ram_dataspace_capability ds) override
	{
		_ram_dataspaces.apply<Ram_ds &>(Ram_ds::id(ds), [&] (Ram_ds &ram_ds) {
			_wipe_ram_ds(ram_ds); });

		_real.call<Rpc_free_ram>(ds);
	}

	size_t ram_size(Ram_dataspace_capability ds) override
	{
		return _real.call<Rpc_ram_size>(ds);
	}

	Ram_quota ram_quota() const override {
		return _real.call<Rpc_ram_quota>(); }

	Ram_quota used_ram()  const override {
		return _real.call<Rpc_used_ram>(); }

	Capability<Native_pd> native_pd() override {
		return _real.call<Rpc_native_pd>(); }

	Capability<System_control> system_control_cap(Affinity::Location const location) override {
		return _real.call<Rpc_system_control_cap>(location); }

	addr_t dma_addr(Ram_dataspace_capability ds) override {
		return _real.call<Rpc_dma_addr>(ds); }

	Attach_dma_result attach_dma(Dataspace_capability ds, addr_t at) override {
		return _real.call<Rpc_attach_dma>(ds, at); }
};

#endif /* _INFERIOR_PD_H_ */
