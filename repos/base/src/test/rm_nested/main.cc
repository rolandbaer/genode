/*
 * \brief  Testing nested region maps
 * \author Norman Feske
 * \date   2008-09-27
 *
 * The program uses two threads. A local fault-handler thread waits for fault
 * signals regarding a sub-region maps that is mapped into the local
 * address space as a dataspace. If a fault occurs, this thread allocates a new
 * dataspace and attaches it to the faulting address to resolve the fault. The
 * main thread performs memory accesses at the local address range that is
 * backed by the region map. Thereby, it triggers region-map faults.
 */

/*
 * Copyright (C) 2008-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/component.h>
#include <rm_session/connection.h>
#include <region_map/client.h>
#include <dataspace/client.h>

using namespace Genode;


enum {
	MANAGED_SIZE = 0x00010000,
	PAGE_SIZE    = 4096,
};


/**
 * Region-manager fault handler resolves faults by attaching new dataspaces
 */
class Local_fault_handler : public Entrypoint
{
	private:

		Env                                &_env;
		Region_map                         &_region_map;
		Signal_handler<Local_fault_handler> _handler;
		volatile unsigned                   _fault_cnt { 0 };

		void _handle_fault()
		{
			Region_map::Fault fault = _region_map.fault();

			_fault_cnt = _fault_cnt + 1;

			log("region-map fault is ",
			    fault.type == Region_map::Fault::Type::READ  ? "READ_FAULT"  :
			    fault.type == Region_map::Fault::Type::WRITE ? "WRITE_FAULT" :
			    fault.type == Region_map::Fault::Type::EXEC  ? "EXEC_FAULT"  : "READY",
			    ", pf_addr=", Hex(fault.addr, Hex::PREFIX));

			log("allocate dataspace and attach it to sub region map");
			Dataspace_capability ds = _env.ram().alloc(PAGE_SIZE);
			_region_map.attach(ds, {
				.size       = { },   .offset    = { },
				.use_at     = true,  .at        = fault.addr & ~(PAGE_SIZE - 1),
				.executable = { },   .writeable = true })
			.with_result(
				[&] (Region_map::Range) { },
				[&] (Region_map::Attach_error) { warning("attach to sub rm failed"); }
			);

			log("returning from handle_fault");
		}

	public:

		Local_fault_handler(Genode::Env &env, Region_map &region_map)
		: Entrypoint(env, sizeof(addr_t)*2048, "local_fault_handler",
		             Affinity::Location()),
		  _env(env),
		  _region_map(region_map),
		  _handler(*this, *this, &Local_fault_handler::_handle_fault)
		{
			region_map.fault_handler(_handler);

			log("fault handler: waiting for fault signal");
		}

		void dissolve() { Entrypoint::dissolve(_handler); }

		unsigned fault_count() { asm volatile ("":::"memory"); return _fault_cnt; }
};


using Attach_attr = Region_map::Attr;


static Attach_attr const
	attr_rw           { .size       = { },   .offset    = { },
	                    .use_at     = { },   .at        = { },
	                    .executable = { },   .writeable = true },
	attr_ro_at_0x1000 { .size       = { },   .offset    = { },
	                    .use_at     = true,  .at        = 0x1000,
	                    .executable = { },   .writeable = { } };


static void *attach(Region_map &rm, Dataspace_capability ds, Attach_attr attr)
{
	return rm.attach(ds, attr).convert<void *>(
		[&] (Region_map::Range range) { return (void *)range.start; },
		[&] (Region_map::Attach_error) {
			error("remote read-only attach unexpectedly failed");
			return nullptr; });
}


static void *attach(Env::Local_rm &local_rm, Dataspace_capability ds, Attach_attr attr)
{
	return local_rm.attach(ds, attr).convert<void *>(
		[&] (Env::Local_rm::Attachment &a) {
			a.deallocate = false; return a.ptr; },
		[&] (Env::Local_rm::Error) {
			error("local read-only attach unexpectedly failed");
			return nullptr; });
}


void nested_regions(Genode::Env &env)
{
	enum {
		MANAGED_REGION_TOP_SIZE    = 40UL * 1024,
		MANAGED_REGION_SHIM1_SIZE  = 24UL * 1024,
		MANAGED_REGION_SHIM2_SIZE  = 16UL * 1024,
		MANAGED_REGION_BOTTOM_SIZE = 40UL * 1024,
	};

	Rm_connection     rm(env);

	/* top region */
	Region_map_client rm_top(rm.create(MANAGED_REGION_TOP_SIZE));
	Dataspace_client  rm_top_client(rm_top.dataspace());

	void         *ptr_top  = attach(env.rm(), rm_top.dataspace(), attr_rw);
	addr_t const  addr_top = reinterpret_cast<addr_t>(ptr_top);
	log(" region top        ",
	    Hex_range<addr_t>(addr_top, rm_top_client.size()));

	/* shim region 1 */
	Region_map_client rm_shim1(rm.create(MANAGED_REGION_SHIM1_SIZE));
	Dataspace_client  rm_shim1_client(rm_shim1.dataspace());
	void         *ptr_shim1  = attach(rm_top, rm_shim1.dataspace(), attr_rw);
	addr_t const  addr_shim1 = reinterpret_cast<addr_t>(ptr_shim1);

	/* shim region 2 */
	Region_map_client rm_shim2(rm.create(MANAGED_REGION_SHIM2_SIZE));
	Dataspace_client  rm_shim2_client(rm_shim2.dataspace());
	void         *ptr_shim2  = attach(rm_top, rm_shim2.dataspace(), attr_rw);
	addr_t const  addr_shim2 = reinterpret_cast<addr_t>(ptr_shim2);

	log(" region shim       ",
	    Hex_range<addr_t>(addr_top + addr_shim1, rm_shim1_client.size()), " ",
	    Hex_range<addr_t>(addr_top + addr_shim2, rm_shim2_client.size()));

	/* attach some memory to region 2 as readonly and touch/map it */
	size_t const         shim2_ram_size = PAGE_SIZE * 2;
	Dataspace_capability shim2_ram_ds = env.ram().alloc(shim2_ram_size);

	void * const ptr_shim2_ram  = attach(rm_shim2, shim2_ram_ds, attr_ro_at_0x1000);
	addr_t const addr_shim2_ram = reinterpret_cast<addr_t>(ptr_shim2_ram);
	addr_t const read_shim2     = addr_top + addr_shim2 + addr_shim2_ram;

	log("  attached mem                         ",
	    (sizeof(void *) == 8) ? "                " : "",
	    Hex_range<addr_t>(read_shim2, shim2_ram_size));

	log("  read     mem                         ",
	    (sizeof(void *) == 8) ? "                " : "",
	    Hex_range<addr_t>(read_shim2, shim2_ram_size), " value=",
	    Hex(*(unsigned *)(read_shim2)));

	/* bottom region */
	Region_map_client rm_bottom(rm.create(MANAGED_REGION_BOTTOM_SIZE));
	Dataspace_client  rm_bottom_client(rm_bottom.dataspace());
	size_t const  size_bottom = MANAGED_REGION_BOTTOM_SIZE - MANAGED_REGION_SHIM2_SIZE;

	void const * const ptr_bottom = attach(rm_shim1, rm_bottom.dataspace(), {
		.size       = size_bottom,  .offset    = { },
		.use_at     = { },          .at        = { },
		.executable = { },          .writeable = { } });

	addr_t const  addr_bottom = reinterpret_cast<addr_t>(ptr_bottom);

	log("   bottom shim (r) ",
	    Hex_range<addr_t>(addr_top + addr_shim1 + addr_bottom, rm_bottom_client.size()));
	log("   bottom shim (s) ",
	    Hex_range<addr_t>(addr_top + addr_shim1 + addr_bottom, size_bottom));

	/* attach some memory to bottom as writeable */
	Dataspace_capability bottom_ram_ds = env.ram().alloc(MANAGED_REGION_BOTTOM_SIZE);
	{
		void * base_rw = attach(env.rm(), bottom_ram_ds, attr_rw);
		memset(base_rw, 0xff, MANAGED_REGION_BOTTOM_SIZE);
		env.rm().detach(addr_t(base_rw));
	}

	void const * const ptr_bottom_ram = attach(rm_bottom, bottom_ram_ds, {
		.size       = { },   .offset    = { },
		.use_at     = true,  .at        = 0,
		.executable = { },   .writeable = true });

	addr_t const addr_bottom_ram = reinterpret_cast<addr_t>(ptr_bottom_ram);
	addr_t const write_bottom    = addr_top + addr_shim1 + addr_bottom + addr_bottom_ram;

	log("    attached mem   ",
	    Hex_range<addr_t>(write_bottom, size_bottom));

	log("    wrote    mem   ",
	    Hex_range<addr_t>(write_bottom, size_bottom), " with value=",
	    Hex(*(unsigned *)write_bottom));

	log(" try reading mem  ", Hex(read_shim2), " - should succeed");
	unsigned value = *(unsigned *)(read_shim2);
	if (value != 0)
		error(" wrong content read - expected 0, got ", Hex(value));

	log(" try reading mem  ", Hex(read_shim2 + PAGE_SIZE), " - should succeed");
	value = *(unsigned *)(read_shim2 + PAGE_SIZE);
	if (value != 0)
		error(" wrong content read - expected 0, got ", Hex(value));

	Local_fault_handler fault_handler(env, rm_shim2);

	log(" try reading mem  ", Hex(read_shim2 - PAGE_SIZE), " - should fail");
	value = *(unsigned *)(read_shim2 - PAGE_SIZE);

	if (fault_handler.fault_count() != 1)
		error(" could read memory without region attached, value=", Hex(value));
}


void Component::construct(Genode::Env &env)
{
	log("--- nested region map test ---");

	{
		/*
		 * Initialize sub region map and set up a local fault handler for it.
		 */
		Rm_connection rm(env);
		Region_map_client   region_map(rm.create(MANAGED_SIZE));
		Local_fault_handler fault_handler(env, region_map);

		/*
		 * Attach region map as dataspace to the local address space.
		 */
		void *addr = attach(env.rm(), region_map.dataspace(), attr_rw);

		log("attached sub dataspace at local address ", addr);
		Dataspace_client client(region_map.dataspace());
		log("sub dataspace size is ", client.size(), " should be ",
			(size_t)MANAGED_SIZE);

		/*
		 * Walk through the address range belonging to the region map
		 */
		char *managed = (char *)addr;
		for (int i = 0; i < MANAGED_SIZE; i += PAGE_SIZE/16) {
			log("write to ", (void*)&managed[i]);
			managed[i] = 13;
		}

		log("test destruction of region_map");
		Capability<Region_map> rcap = rm.create(4096);
		rm.destroy(rcap);

		log("test multiple nested regions stacked");
		nested_regions(env);
	}

	log("--- finished nested region map test ---");
}
