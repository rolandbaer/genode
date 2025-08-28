/*
 * \brief  Representation of AHCI devices
 * \author Norman Feske
 * \date   2024-04-21
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _MODEL__AHCI_DEVICE_H_
#define _MODEL__AHCI_DEVICE_H_

#include <model/storage_device.h>

namespace Sculpt {

	struct Ahci_device;

	using Ahci_devices = List_model<Ahci_device>;
};


struct Sculpt::Ahci_device : List_model<Ahci_device>::Element, Storage_device
{
	using Model = String<16>;

	Model const model;

	static Port _port(Node const &node)
	{
		return node.attribute_value("num", Port());
	}

	static Capacity _capacity(Node const &node)
	{
		return { node.attribute_value("block_size",  0ULL)
		       * node.attribute_value("block_count", 0ULL) };
	}

	Ahci_device(Env &env, Allocator &alloc, Node const &node,
	            Storage_device::Action &action)
	:
		Storage_device(env, alloc, "ahci", _port(node), _capacity(node), action),
		model(node.attribute_value("model", Model()))
	{ }

	bool matches(Node const &node) const { return _port(node) == port; }

	static bool type_matches(Node const &) { return true; }
};

#endif /* _MODEL__AHCI_DEVICE_H_ */
