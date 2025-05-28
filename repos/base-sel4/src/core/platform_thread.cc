/*
 * \brief   Thread facility
 * \author  Norman Feske
 * \date    2015-05-01
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* core includes */
#include <platform_thread.h>
#include <platform_pd.h>

/* base-internal includes */
#include <base/internal/capability_space_sel4.h>
#include <base/internal/native_utcb.h>

/* seL4 includes */
#include <sel4/benchmark_utilisation_types.h>

using namespace Core;


/*****************************************************
 ** Implementation of the install_mapping interface **
 *****************************************************/

class Platform_thread_registry : Noncopyable
{
	private:

		List<Platform_thread> _threads { };
		Mutex                 _mutex   { };

	public:

		void insert(Platform_thread &thread)
		{
			Mutex::Guard guard(_mutex);
			_threads.insert(&thread);
		}

		void remove(Platform_thread &thread)
		{
			Mutex::Guard guard(_mutex);
			_threads.remove(&thread);
		}

		bool install_mapping(Mapping const &mapping, unsigned long pager_object_badge)
		{
			unsigned installed = 0;
			bool     result    = true;

			Mutex::Guard guard(_mutex);

			for (Platform_thread *t = _threads.first(); t; t = t->next()) {
				if (t->pager_object_badge() == pager_object_badge) {
					bool ok = t->install_mapping(mapping);
					if (!ok)
						result = false;
					installed ++;
				}
			}

			if (installed != 1) {
				error("install mapping is wrong ", installed,
				      " result=", result);
				result = false;
			}

			return result;
		}
};


Platform_thread_registry &platform_thread_registry()
{
	static Platform_thread_registry inst;
	return inst;
}


bool Core::install_mapping(Mapping const &mapping, unsigned long pager_object_badge)
{
	return platform_thread_registry().install_mapping(mapping, pager_object_badge);
}


/********************************************************
 ** Utilities to support the Platform_thread interface **
 ********************************************************/

static void prepopulate_ipc_buffer(Ipc_buffer_phys const &ipc_buffer_phys,
                                   Cap_sel         const &ep_sel,
                                   Cap_sel         const &lock_sel,
                                   Utcb_virt       const &utcb_virt)
{
	ipc_buffer_phys.with_result([&](auto &result) {

		/* IPC buffer is one page */
		size_t const page_rounded_size = get_page_size();

		/* allocate range in core's virtual address space */
		platform().region_alloc().try_alloc(page_rounded_size).with_result(

			[&] (Range_allocator::Allocation &virt) {

				/* map the IPC buffer to core-local virtual addresses */
				map_local(addr_t(result.ptr), addr_t(virt.ptr), 1);

				/* populate IPC buffer with thread information */
				Native_utcb &utcb = *(Native_utcb *)virt.ptr;
				utcb.ep_sel  (ep_sel  .value());
				utcb.lock_sel(lock_sel.value());
				utcb.ipcbuffer(utcb_virt);

				/* unmap IPC buffer from core */
				if (!unmap_local((addr_t)virt.ptr, 1))
					error("could not unmap core virtual address ",
					      virt.ptr, " in ", __PRETTY_FUNCTION__);
			},

			[&] (Alloc_error) {
				error("could not allocate virtual address range in core of size ",
				      page_rounded_size);
			}
		);
	}, [&](auto) { error("prepopulate ipc buffer failed"); });
}


/*******************************
 ** Platform_thread interface **
 *******************************/

void Platform_thread::start(void *ip, void *sp, unsigned int)
{
	if (!_pager || constructed.failed())
		return;

	/* pager endpoint in core */
	Cap_sel const pager_sel(Capability_space::ipc_cap_data(_pager->cap()).sel);

	/* install page-fault handler endpoint selector to the PD's CSpace */
	_pd.with_cspace_cnode(_fault_handler_sel, [&] (auto &cnode) {
		cnode.copy(platform_specific().core_cnode(),
		           pager_sel, _fault_handler_sel);
	});

	/* install the thread's endpoint selector to the PD's CSpace */
	_pd.with_cspace_cnode(_ep_sel, [&] (auto &cnode) {
		cnode.copy(platform_specific().core_cnode(), _info.ep_sel, _ep_sel);
	});

	/* install the thread's notification object to the PD's CSpace */
	_pd.with_cspace_cnode(_lock_sel, [&] (auto &cnode) {
		cnode.mint(platform_specific().core_cnode(), _info.lock_sel, _lock_sel);
	});

	/*
	 * Populate the thread's IPC buffer with initial information about the
	 * thread. Once started, the thread picks up this information in the
	 * 'Thread::_thread_bootstrap' method.
	 */
	prepopulate_ipc_buffer(_info.ipc_phys, _ep_sel, _lock_sel, _utcb);

	/* bind thread to PD and CSpace */
	seL4_CNode_CapData const guard_cap_data =
		seL4_CNode_CapData_new(0, CONFIG_WORD_SIZE - _pd.cspace_size_log2());

	seL4_CNode_CapData const no_cap_data = { { 0 } };

	_pd.with_cspace_cnode_1st([&](auto const &cnode_1st) {
		int const ret = seL4_TCB_SetSpace(_info.tcb_sel.value(),
		                                  _fault_handler_sel.value(),
		                                  cnode_1st.sel().value(),
		                                  guard_cap_data.words[0],
		                                  _pd.page_directory_sel().value(),
		                                  no_cap_data.words[0]);
		ASSERT(ret == 0);

		start_sel4_thread(_info.tcb_sel, (addr_t)ip, (addr_t)(sp), _location.xpos(),
		                  _utcb.addr);
	});
}


void Platform_thread::pause()
{
	int const ret = seL4_TCB_Suspend(_info.tcb_sel.value());
	if (ret != seL4_NoError)
		error("pausing thread failed with ", ret);
}


void Platform_thread::resume()
{
	int const ret = seL4_TCB_Resume(_info.tcb_sel.value());
	if (ret != seL4_NoError)
		error("pausing thread failed with ", ret);
}


void Platform_thread::state(Thread_state) { }


bool Platform_thread::install_mapping(Mapping const &mapping)
{
	return _pd.install_mapping(mapping, name());
}


Platform_thread::Platform_thread(Platform_pd &pd, Rpc_entrypoint &, Ram_allocator &,
                                 Local_rm &, size_t, const char *name,
                                 unsigned priority, Affinity::Location location,
                                 addr_t utcb)
:
	_name(name),
	_utcb(utcb ? utcb : addr_t(INITIAL_IPC_BUFFER_VIRT)),
	_pd(pd), _location(location),
	_priority((uint16_t)(Cpu_session::scale_priority(CONFIG_NUM_PRIORITIES, priority)))

{
	static_assert(CONFIG_NUM_PRIORITIES == 256, " unknown priority configuration");

	if (_priority > 0)
		_priority -= 1;

	platform_thread_registry().insert(*this);

	platform_specific().core_sel_alloc().alloc().with_result([&](auto sel) {
		_pager_obj_sel = Cap_sel(unsigned(sel));
	}, [&](auto) { });

	if (!_pager_obj_sel.value())
		return;

	_info.init(_utcb, _priority);

	if (!_info.valid())
		return;

	_pd.alloc_thread_selectors([&](auto &sel) {
		bool ok = true;

		/* allocate fault handler selector in the PD's CSpace */
		if (ok)
			sel.alloc().with_result(
				[&](auto idx) { _fault_handler_sel = Cap_sel(unsigned(idx)); },
				[&](auto    ) { ok = false; });

		/* allocate endpoint selector in the PD's CSpace */
		if (ok)
			sel.alloc().with_result(
				[&](auto idx) { _ep_sel = Cap_sel(unsigned(idx)); },
				[&](auto    ) { ok = false; });
		if (ok)
			sel.alloc().with_result(
				[&](auto idx) { _vcpu_sel = Cap_sel(unsigned(idx)); },
				[&](auto    ) { ok = false; });
		if (ok)
			sel.alloc().with_result(
				[&](auto idx) { _vcpu_notify_sel = Cap_sel(unsigned(idx)); },
				[&](auto    ) { ok = false; });

		/* allocate asynchronous selector used for locks in the PD's CSpace */
		if (ok) {
			if (main_thread())
				_lock_sel = Cap_sel(INITIAL_SEL_LOCK);
			else
				sel.alloc().with_result(
					[&](auto idx) { _lock_sel = Cap_sel(unsigned(idx)); },
					[&](auto    ) { ok = false; });
		}

		if (ok && _pd.map_ipc_buffer(_info.ipc_phys, _utcb)) {
			_bound_to_pd = true;
			constructed = Ok();
		}
	});

	if (_bound_to_pd)
		return;

	/* revert allocations */
	if (_fault_handler_sel.value()) {
		_pd.free_sel(_fault_handler_sel);
		_fault_handler_sel = Cap_sel { 0 };
	}
	if (_ep_sel.value()) {
		_pd.free_sel(_ep_sel);
		_ep_sel = Cap_sel { 0 };
	}
	if (_vcpu_sel.value()) {
		_pd.free_sel(_vcpu_sel);
		_vcpu_sel = Cap_sel { 0 };
	}
	if (_vcpu_notify_sel.value()) {
		_pd.free_sel(_vcpu_notify_sel);
		_vcpu_notify_sel = Cap_sel { 0 };
	}
	if (_lock_sel.value()) {
		_pd.free_sel(_lock_sel);
		_lock_sel = Cap_sel { 0 };
	}
}


Platform_thread::~Platform_thread()
{
	if (_info.valid())
		seL4_TCB_Suspend(_info.tcb_sel.value());

	if (_bound_to_pd) {
		if (!main_thread())
			_pd.free_sel(_lock_sel);

		_pd.free_sel(_fault_handler_sel);
		_pd.free_sel(_ep_sel);
		_pd.free_sel(_vcpu_sel);
		_pd.free_sel(_vcpu_notify_sel);

		_pd.unmap_ipc_buffer(_utcb);
	}

	if (_pager) {
		Cap_sel const pager_sel(Capability_space::ipc_cap_data(_pager->cap()).sel);
		seL4_CNode_Revoke(seL4_CapInitThreadCNode, pager_sel.value(), 32);
	}

	if (_info.valid()) {
		seL4_CNode_Revoke(seL4_CapInitThreadCNode, _info.lock_sel.value(), 32);
		seL4_CNode_Revoke(seL4_CapInitThreadCNode, _info.ep_sel.value(), 32);
	}

	_info.destruct();

	platform_thread_registry().remove(*this);

	if (_pager_obj_sel.value())
		platform_specific().core_sel_alloc().free(_pager_obj_sel);
}


Trace::Execution_time Platform_thread::execution_time() const
{
	if (constructed.failed() || !Thread::myself() || !Thread::myself()->utcb())
		return { 0, 0, 10000, _priority };

	Thread &myself = *Thread::myself();

	seL4_IPCBuffer &ipc_buffer = *reinterpret_cast<seL4_IPCBuffer *>(myself.utcb());
	uint64_t const * values    =  reinterpret_cast<uint64_t *>(ipc_buffer.msg);

	/* kernel puts execution time on ipc buffer of calling thread */
	seL4_BenchmarkGetThreadUtilisation(_info.tcb_sel.value());

	uint64_t const ec_time = values[BENCHMARK_TCB_UTILISATION];
	uint64_t const sc_time = 0; /* not supported */
	return { ec_time, sc_time, 10000, _priority};
}


void Platform_thread::setup_vcpu(Cap_sel ept, Cap_sel notification)
{
	if (!_info.init_vcpu(platform_specific(), ept)) {
		error("creating vCPU failed");
		return;
	}

	/* install the thread's endpoint selector to the PD's CSpace */
	_pd.with_cspace_cnode(_vcpu_sel, [&](auto &cnode) {
		cnode.copy(platform_specific().core_cnode(), _info.vcpu_sel, _vcpu_sel);
	});

	_pd.with_cspace_cnode(_vcpu_notify_sel, [&](auto &cnode) {
		cnode.copy(platform_specific().core_cnode(), notification,
		           _vcpu_notify_sel);
	});

	prepopulate_ipc_buffer(_info.ipc_phys, _vcpu_sel, _vcpu_notify_sel, _utcb);
}
