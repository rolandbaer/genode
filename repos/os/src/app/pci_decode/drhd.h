/*
 * \brief  DMA remapping hardware reporting from ACPI information in list models
 * \author Johannes Schlatow
 * \date   2023-08-14
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */


#include <base/heap.h>
#include <pci/types.h>
#include <util/list_model.h>

using namespace Genode;
using namespace Pci;


struct Drhd : List_model<Drhd>::Element
{
	using Drhd_name = String<16>;

	enum Scope { INCLUDE_PCI_ALL, EXPLICIT };

	addr_t   addr;
	size_t   size;
	unsigned segment;
	Scope    scope;
	unsigned number;

	struct Device : Registry<Device>::Element
	{
		enum { IOAPIC = 0x3 };

		Bdf bdf;
		uint8_t type;
		uint8_t id;

		Device(Registry<Device> &registry, Bdf bdf, uint8_t type, uint8_t id)
		: Registry<Device>::Element(registry, *this), bdf(bdf), type(type), id(id)
		{ }
	};

	Registry<Device> devices { };

	Drhd(addr_t addr, size_t size, unsigned segment, Scope scope, unsigned number)
	: addr(addr), size(size), segment(segment), scope(scope), number(number)
	{ }

	Drhd_name name() const { return Drhd_name("drhd", number); }

	bool matches(Node const &node) const
	{
		return addr == node.attribute_value("phys", 0UL);
	}

	static bool type_matches(Node const &node)
	{
		return node.has_type("drhd");
	}
};
