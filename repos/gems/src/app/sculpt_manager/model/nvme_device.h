/*
 * \brief  Representation of NVMe devices
 * \author Norman Feske
 * \date   2024-04-22
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _MODEL__NVME_DEVICE_H_
#define _MODEL__NVME_DEVICE_H_

#include <model/storage_device.h>

namespace Sculpt {

	struct Nvme_device;

	using Nvme_devices = List_model<Nvme_device>;
};


struct Sculpt::Nvme_device : List_model<Nvme_device>::Element, Storage_device
{
	using Model = String<16>;

	Model const model;

	static Port _port(Node const &node)
	{
		return node.attribute_value("id", Port());
	}

	static Capacity _capacity(Node const &node)
	{
		return { node.attribute_value("block_size",  0ULL)
		       * node.attribute_value("block_count", 0ULL) };
	}

	Nvme_device(Env &env, Allocator &alloc, Model const &model, Node const &node,
	            Storage_device::Action &action)
	:
		Storage_device(env, alloc, "nvme", _port(node), _capacity(node), action),
		model(model)
	{ }

	bool matches(Node const &node) const { return _port(node) == port; }

	static bool type_matches(Node const &) { return true; }
};

#endif /* _MODEL__NVME_DEVICE_H_ */
