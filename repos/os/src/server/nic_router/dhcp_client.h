/*
 * \brief  DHCP client state model
 * \author Martin Stein
 * \date   2016-08-24
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _DHCP_CLIENT_H_
#define _DHCP_CLIENT_H_

/* local includes */
#include <cached_timer.h>
#include <packet_result.h>

/* Genode includes */
#include <net/dhcp.h>

namespace Net {

	class Domain;
	class Configuration;
	class Interface;
	class Ethernet_frame;
	class Dhcp_client;
}

class Net::Dhcp_client
{
	private:

		enum class State
		{
			INIT = 0, SELECT = 1, REQUEST = 2, BOUND = 3, RENEW = 4, REBIND = 5
		};

		Interface                            &_interface;
		State                                 _state { State::INIT };
		Timer::One_shot_timeout<Dhcp_client>  _timeout;
		Genode::uint64_t                      _lease_time_sec = 0;

		void _handle_timeout(Genode::Duration);

		void _rerequest(State next_state, Domain &domain);

		Genode::Microseconds _rerequest_timeout(unsigned lease_time_div_log2, Domain &domain);

		void _set_state(State state, Genode::Microseconds timeout);

		void _send(Dhcp_packet::Message_type msg_type,
		           Ipv4_address              client_ip,
		           Ipv4_address              server_ip,
		           Ipv4_address              requested_ip,
		           Genode::size_t            pkt_size);

	public:

		Dhcp_client(Cached_timer      &timer,
		            Interface         &interface);

		[[nodiscard]] Packet_result handle_dhcp_reply(Dhcp_packet &dhcp, Domain &domain);

		void discover();
};

#endif /* _DHCP_CLIENT_H_ */
