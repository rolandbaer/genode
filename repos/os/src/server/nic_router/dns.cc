/*
 * \brief  Utilities for handling DNS configurations
 * \author Martin Stein
 * \date   2020-11-17
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* local includes */
#include <dns.h>
#include <domain.h>
#include <configuration.h>

/* Genode includes */
#include <base/node.h>

using namespace Net;
using namespace Genode;


/****************
 ** Dns_server **
 ****************/

Dns_server::Dns_server(Ipv4_address const &ip)
:
	_ip { ip }
{ }


bool Dns_server::equal_to(Dns_server const &server) const
{
	return _ip == server._ip;
}


/*********************
 ** Dns_domain_name **
 *********************/

void Dns_domain_name::set_to(Dns_domain_name const &name)
{
	if (name.valid()) {
		name.with_string([&] (String const &string) {
			if (_string_ptr) {
				*_string_ptr = string;
			} else {
				_string_ptr = new (_alloc) String { string };
			}
		});
	} else {
		set_invalid();
	}
}


void Dns_domain_name::set_to(Node::Attribute const &name)
{
	if (name.value.num_bytes < STRING_CAPACITY) {
		if (_string_ptr) {
			*_string_ptr = Cstring { name.value.start, name.value.num_bytes };
		} else {
			_string_ptr = new (_alloc)
				String { Cstring { name.value.start, name.value.num_bytes } };
		}
	} else {
		set_invalid();
	}
}


void Dns_domain_name::set_to(Dhcp_packet::Domain_name const &name_option)
{
	name_option.with_string([&] (char const *base, size_t size) {
		if (size < STRING_CAPACITY) {
			if (_string_ptr) {
				*_string_ptr = Cstring { base, size };
			} else {
				_string_ptr = new (_alloc) String { Cstring { base, size } };
			}
		} else {
			set_invalid();
		}
	});
}


void Dns_domain_name::set_invalid()
{
	if (_string_ptr) {
		_alloc.free(_string_ptr, sizeof(String));
		_string_ptr = nullptr;
	}
}


bool Dns_domain_name::equal_to(Dns_domain_name const &other) const
{
	if (_string_ptr) {
		if (other._string_ptr)
			return *_string_ptr == *other._string_ptr;
		return false;
	}
	return !other._string_ptr;
}


Dns_domain_name::Dns_domain_name(Genode::Allocator &alloc)
:
	_alloc { alloc }
{ }


Dns_domain_name::~Dns_domain_name()
{
	set_invalid();
}
