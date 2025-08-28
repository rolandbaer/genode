/*
 * \brief  VM session component for 'base-hw'
 * \author Stefan Kalkowski
 * \author Benjamin Lamowski
 * \date   2024-09-20
 */

/*
 * Copyright (C) 2015-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__VM_SESSION_COMPONENT_H_
#define _CORE__VM_SESSION_COMPONENT_H_

/* base includes */
#include <base/allocator.h>
#include <base/session_object.h>
#include <base/registry.h>
#include <vm_session/vm_session.h>
#include <dataspace/capability.h>

/* core includes */
#include <cpu_thread_component.h>
#include <region_map_component.h>
#include <trace/source_registry.h>

/* base-hw includes */
#include <spec/x86_64/ept.h>
#include <spec/x86_64/hpt.h>
#include <vcpu.h>
#include <vmid_allocator.h>
#include <guest_memory.h>
#include <phys_allocated.h>


namespace Core {
	template <typename TABLE> class Vm_session_component;
	using Vmx_session_component = Vm_session_component<Hw::Ept>;
	using Svm_session_component = Vm_session_component<Hw::Hpt>;
}


template <typename TABLE>
class Core::Vm_session_component
:
	public Session_object<Vm_session>,
	public Revoke
{
	private:

		using Vm_page_table_array = typename TABLE::Array;


		/*
		 * Noncopyable
		 */
		Vm_session_component(Vm_session_component const &);
		Vm_session_component &operator = (Vm_session_component const &);

		struct Detach : Region_map_detach
		{
			Vm_session_component &_session;

			Detach(Vm_session_component &session) : _session(session)
			{ }

			void detach_at(addr_t at) override
			{
				_session._detach_at(at);
			}

			void reserve_and_flush(addr_t at) override
			{
				_session._reserve_and_flush(at);
			}

			void unmap_region(addr_t base, size_t size) override
			{
				Genode::error(__func__, " unimplemented ", base, " ", size);
			}
		} _detach { *this };

		Registry<Registered<Vcpu>>          _vcpus { };

		Registry<Revoke>::Element           _elem;
		Rpc_entrypoint                     &_ep;
		Accounted_ram_allocator             _accounted_ram_alloc;
		Local_rm                           &_local_rm;
		Heap                                _heap;
		Phys_allocated<TABLE>               _table;
		Phys_allocated<Vm_page_table_array> _table_array;
		Guest_memory                        _memory;
		Vmid_allocator                     &_vmid_alloc;
		uint8_t                             _remaining_print_count { 10 };

		Kernel::Vcpu::Identity _id {
			_vmid_alloc.alloc().convert<unsigned>(
				[] (addr_t const id)       -> unsigned { return unsigned(id); },
				[] (Vmid_allocator::Error) -> unsigned { throw Service_denied(); }),
			(void *)_table.phys_addr()
		};

		void _detach_at(addr_t addr)
		{
			_memory.detach_at(addr,
				[&](addr_t vm_addr, size_t size) {
				_table.obj.remove(vm_addr, size, _table_array.obj.alloc()); });
		}

		void _reserve_and_flush(addr_t addr)
		{
			_memory.reserve_and_flush(addr, [&](addr_t vm_addr, size_t size) {
				_table.obj.remove(vm_addr, size, _table_array.obj.alloc()); });
		}

	public:

		Vm_session_component(Registry<Revoke> &registry,
		                     Vmid_allocator &vmid_alloc,
		                     Rpc_entrypoint &ds_ep,
		                     Resources resources,
		                     Label const &label,
		                     Diag diag,
		                     Ram_allocator &ram_alloc,
		                     Local_rm &local_rm,
		                     Trace::Source_registry &)
		:
			Session_object(ds_ep, resources, label, diag),
			_elem(registry, *this),
			_ep(ds_ep),
			_accounted_ram_alloc(ram_alloc, _ram_quota_guard(), _cap_quota_guard()),
			_local_rm(local_rm),
			_heap(_accounted_ram_alloc, local_rm),
			_table(_ep, _accounted_ram_alloc, _local_rm),
			_table_array(_ep, _accounted_ram_alloc, _local_rm,
					[] (Phys_allocated<Vm_page_table_array> &table_array, auto *obj_ptr) {
						construct_at<Vm_page_table_array>(obj_ptr, [&] (void *virt) {
						return table_array.phys_addr() + ((addr_t) obj_ptr - (addr_t)virt);
						});
					}),
			_memory(_accounted_ram_alloc, local_rm),
			_vmid_alloc(vmid_alloc)
		{ }

		~Vm_session_component()
		{
			_vcpus.for_each([&] (Registered<Vcpu> &vcpu) {
				destroy(_heap, &vcpu); });

			(void)_vmid_alloc.free(_id.id);
		}

		void revoke_signal_context(Signal_context_capability cap) override
		{
			_vcpus.for_each([&] (Vcpu &vcpu) {
				vcpu.revoke_signal_context(cap); });
		}


		/**************************
		 ** Vm session interface **
		 **************************/

		void attach(Dataspace_capability cap, addr_t guest_phys, Attach_attr attr) override
		{
			bool out_of_tables   = false;
			bool invalid_mapping = false;

			auto const &map_fn = [&](addr_t vm_addr, addr_t phys_addr, size_t size) {
				Page_flags const pflags { RW, EXEC, USER, NO_GLOBAL, RAM, CACHED };

				Hw::Page_table::Result result =
					_table.obj.insert(vm_addr, phys_addr, size, pflags,
					                  _table_array.obj.alloc());
				result.with_error([&] (Hw::Page_table_error e) {
					if (e == Hw::Page_table_error::INVALID_RANGE) {
						invalid_mapping = true;
						if (_remaining_print_count) {
							Genode::error("Invalid mapping ", Genode::Hex(phys_addr), " -> ",
							               Genode::Hex(vm_addr), " (", size, ")");
							_remaining_print_count--;
						}
					} else {
						out_of_tables = true;
						if (_remaining_print_count) {
							Genode::error("Translation table needs too much RAM");
							_remaining_print_count--;
						}
					}
				});
			};

			if (!cap.valid())
				throw Invalid_dataspace();

			/* check dataspace validity */
			_ep.apply(cap, [&] (Dataspace_component *ptr) {
				if (!ptr)
					throw Invalid_dataspace();

				Dataspace_component &dsc = *ptr;

				Guest_memory::Attach_result result =
					_memory.attach(_detach, dsc, guest_phys, attr, map_fn);

				if (out_of_tables)
					throw Out_of_ram();

				if (invalid_mapping)
					throw Invalid_dataspace();

				switch (result) {
				case Guest_memory::Attach_result::OK             : break;
				case Guest_memory::Attach_result::INVALID_DS     : throw Invalid_dataspace(); break;
				case Guest_memory::Attach_result::OUT_OF_RAM     : throw Out_of_ram(); break;
				case Guest_memory::Attach_result::OUT_OF_CAPS    : throw Out_of_caps(); break;
				case Guest_memory::Attach_result::REGION_CONFLICT: throw Region_conflict(); break;
				}
			});
		}

		void attach_pic(addr_t) override
		{ }

		void detach(addr_t guest_phys, size_t size) override
		{
			_memory.detach(guest_phys, size, [&](addr_t vm_addr, size_t size) {
				_table.obj.remove(vm_addr, size, _table_array.obj.alloc()); });
		}

		Capability<Native_vcpu> create_vcpu(Thread_capability tcap) override
		{
			Affinity::Location vcpu_location;
			_ep.apply(tcap, [&] (Cpu_thread_component *ptr) {
				if (!ptr) return;
				vcpu_location = ptr->platform_thread().affinity();
			});

			Vcpu &vcpu = *new (_heap)
						Registered<Vcpu>(_vcpus,
						                 _id,
						                 _ep,
						                 _accounted_ram_alloc,
						                 _local_rm,
						                 vcpu_location);

			return vcpu.cap();
		}
};

#endif /* _CORE__VM_SESSION_COMPONENT_H_ */
