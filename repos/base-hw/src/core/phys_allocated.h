/*
 * \brief  Allocate an object with a physical address
 * \author Norman Feske
 * \author Benjamin Lamowski
 * \date   2024-12-02
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__PHYS_ALLOCATED_H_
#define _CORE__PHYS_ALLOCATED_H_

/* base includes */
#include <base/allocator.h>
#include <base/attached_ram_dataspace.h>
#include <util/noncopyable.h>

/* core-local includes */
#include <dataspace_component.h>
#include <types.h>

namespace Core {
	template <typename T>
	class Phys_allocated;
}

template <typename T>
class Core::Phys_allocated : Genode::Noncopyable
{
	private:

		Rpc_entrypoint &_ep;

		Attached_ram_dataspace _ds;

	public:

		T &obj = *_ds.local_addr<T>();

		Phys_allocated(Rpc_entrypoint &ep,
		               Ram_allocator  &ram,
		               Local_rm       &rm)
		:
			_ep(ep), _ds{ram, rm, sizeof(T)}
		{
			construct_at<T>(&obj);
		}

		Phys_allocated(Rpc_entrypoint &ep,
		               Ram_allocator  &ram,
		               Local_rm       &rm,
		               auto const     &construct_fn)
		:
			_ep(ep), _ds{ram, rm, sizeof(T)}
		{
			construct_fn(*this, &obj);
		}

		~Phys_allocated() { obj.~T(); }

		addr_t phys_addr() const
		{
			addr_t phys_addr { };
			 _ep.apply(_ds.cap(), [&](Dataspace_component *dsc) {
				phys_addr = dsc->phys_addr();
			 });

			 return phys_addr;
		}
};

#endif /* _CORE__PHYS_ALLOCATED_H_ */
