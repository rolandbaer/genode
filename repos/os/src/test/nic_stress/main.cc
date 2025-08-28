/*
 * \brief  Stress test for low level NIC interactions
 * \author Martin Stein
 * \date   2019-03-15
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/log.h>
#include <base/heap.h>
#include <base/component.h>
#include <base/attached_rom_dataspace.h>
#include <base/attached_ram_dataspace.h>
#include <nic/packet_allocator.h>
#include <nic_session/connection.h>

namespace Local {

	using namespace Genode;
	struct Bad_args_nic;
	struct Construct_destruct_test;
	struct Main;
}


struct Local::Bad_args_nic : Connection<Nic::Session>
{
	Bad_args_nic(Env &env, size_t ram_quota, size_t tx_buf_size, size_t rx_buf_size,
	             Session::Label const &label)
	:
		Genode::Connection<Nic::Session>(env, label, Ram_quota { ram_quota },
		                                 Args("tx_buf_size=", tx_buf_size, ", "
		                                      "rx_buf_size=", rx_buf_size))
	{ }
};


struct Local::Construct_destruct_test
{
	enum { PKT_SIZE = Nic::Packet_allocator::DEFAULT_PACKET_SIZE };
	enum { BUF_SIZE = 100 * PKT_SIZE };

	using Nic_slot = Constructible<Nic::Connection>;

	Env                         &_env;
	Allocator                   &_alloc;
	Signal_context_capability    _completed_sigh;
	Node                  const &_config;
	Nic::Packet_allocator        _pkt_alloc     { &_alloc };
	Constructible<Bad_args_nic>  _bad_args_nic  { };

	unsigned long _attr_value(auto const &attr, auto const default_value) const
	{
		return _config.with_sub_node("construct_destruct",
			[&] (Node const &node) { return node.attribute_value(attr, default_value); },
			[&]                    { return default_value; });
	}

	unsigned long const _nr_of_rounds   = _attr_value("nr_of_rounds",   10);
	unsigned long const _nr_of_sessions = _attr_value("nr_of_sessions", 10);

	void construct_all(Nic_slot *const nic,
	                   unsigned  const round)
	{
		_bad_args_nic.construct(
			_env, 0, BUF_SIZE, BUF_SIZE, "bad_args");
		for (unsigned idx = 0; idx < _nr_of_sessions; idx++) {
			try {
				nic[idx].construct(_env, &_pkt_alloc, BUF_SIZE, BUF_SIZE);
				log("round ", round + 1, "/", _nr_of_rounds, " nic ", idx + 1,
				    "/", _nr_of_sessions, " mac ", nic[idx]->mac_address());
			}
			catch (...) {
				for (unsigned destruct_idx = 0; destruct_idx < idx; destruct_idx++) {
					nic[destruct_idx].destruct();
					throw;
				}
			}
		}
	}

	void destruct_all(Nic_slot *const nic)
	{
		for (unsigned idx = 0; idx < _nr_of_sessions; idx++) {
			nic[idx].destruct();
		}
	}

	Construct_destruct_test(Env                       &env,
	                        Allocator                 &alloc,
	                        Signal_context_capability  completed_sigh,
	                        Node                const &config)
	:
		_env            { env },
		_alloc          { alloc },
		_completed_sigh { completed_sigh },
		_config         { config }
	{
		if (!_nr_of_rounds && !_nr_of_sessions) {
			Signal_transmitter(_completed_sigh).submit(); }

		size_t           const ram_size { _nr_of_sessions * sizeof(Nic_slot) };
		Attached_ram_dataspace ram_ds   { _env.ram(), _env.rm(), ram_size };
		Nic_slot        *const nic      { ram_ds.local_addr<Nic_slot>() };

		try {
			for (unsigned round = 0; round < _nr_of_rounds; round++) {
				construct_all(nic, round);
				destruct_all(nic);
			}
			Signal_transmitter(_completed_sigh).submit();
		}
		catch (...) {
			error("Construct_destruct_test failed"); }
	}
};


struct Local::Main
{
	Env                                    &_env;
	Heap                                    _heap       { &_env.ram(), &_env.rm() };
	Attached_rom_dataspace                  _config_rom { _env, "config" };
	Node                              const _config     { _config_rom.node() };
	Constructible<Construct_destruct_test>  _test_1     { };

	bool const _exit_support {
		_config.attribute_value("exit_support", true) };

	Signal_handler<Main> _test_completed_handler {
		_env.ep(), *this, &Main::_handle_test_completed };

	void _handle_test_completed()
	{
		if (_test_1.constructed()) {
			_test_1.destruct();
			log("--- finished NIC stress test ---");
			if (_exit_support) {
				_env.parent().exit(0); }
			return;
		}
	}

	Main(Env &env) : _env(env)
	{
		log("--- NIC stress test ---");
		_test_1.construct(_env, _heap, _test_completed_handler, _config);
	}
};


void Component::construct(Genode::Env &env) { static Local::Main main(env); }
