/*
 * \brief  A net interface in form of a signal-driven NIC-packet handler
 * \author Martin Stein
 * \date   2016-08-24
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INTERFACE_H_
#define _INTERFACE_H_

/* local includes */
#include <link.h>
#include <arp_waiter.h>
#include <l3_protocol.h>
#include <dhcp_client.h>
#include <dhcp_server.h>
#include <list.h>
#include <report.h>

/* Genode includes */
#include <net/dhcp.h>
#include <net/icmp.h>

namespace Genode { class Xml_generator; }

namespace Net {

	enum { PKT_STREAM_QUEUE_SIZE = 1024 };

	/*
	 * In order to be compliant to both the Uplink and the Nic packet stream
	 * types, we use the more base types from the Genode namespace here and
	 * combine them with the same parameters as in the Uplink and Nic
	 * namespaces. I.e., we assume the Uplink and Nic packet stream types to
	 * be factually the same although they are logically independent from each
	 * other.
	 */
	using Packet_descriptor    = Genode::Packet_descriptor;
	using Packet_stream_policy = Genode::Packet_stream_policy<Packet_descriptor, PKT_STREAM_QUEUE_SIZE, PKT_STREAM_QUEUE_SIZE, char>;
	using Packet_stream_sink   = Genode::Packet_stream_sink<Packet_stream_policy>;
	using Packet_stream_source = Genode::Packet_stream_source<Packet_stream_policy>;

	using Domain_name = Genode::String<160>;
	class Ipv4_config;
	class Forward_rule_tree;
	class Transport_rule_list;
	class Ethernet_frame;
	class Arp_packet;
	class Interface_policy;
	class Interface;
	using Interface_list = List<Interface>;
	class Interface_link_stats;
	class Interface_object_stats;
	class Dhcp_server;
	class Configuration;
	class Domain;
}


struct Net::Interface_object_stats
{
	Genode::size_t alive     { 0 };
	Genode::size_t destroyed { 0 };

	bool report_empty() const;
	void report(Genode::Xml_generator &xml) const;

	~Interface_object_stats();
};


struct Net::Interface_link_stats
{
	Genode::size_t refused_for_ram           { 0 };
	Genode::size_t refused_for_ports         { 0 };
	Genode::size_t opening                   { 0 };
	Genode::size_t open                      { 0 };
	Genode::size_t closing                   { 0 };
	Genode::size_t closed                    { 0 };
	Genode::size_t dissolved_timeout_opening { 0 };
	Genode::size_t dissolved_timeout_open    { 0 };
	Genode::size_t dissolved_timeout_closing { 0 };
	Genode::size_t dissolved_timeout_closed  { 0 };
	Genode::size_t dissolved_no_timeout      { 0 };
	Genode::size_t destroyed                 { 0 };

	bool report_empty() const;
	void report(Genode::Xml_generator &xml) const;

	~Interface_link_stats();
};


struct Net::Interface_policy : Genode::Interface
{
	virtual Domain_name determine_domain_name() const = 0;

	virtual void handle_config(Configuration const &config) = 0;

	virtual Genode::Session_label const &label() const = 0;

	virtual void handle_domain_ready_state(bool) = 0;

	virtual bool interface_link_state() const = 0;

	virtual bool report_empty() const = 0;

	virtual void report(Genode::Xml_generator &) const = 0;

	virtual ~Interface_policy() { }
};


class Net::Interface : private Interface_list::Element
{
	friend class List<Interface>;
	friend class Genode::List<Interface>;

	private:

		using Signal_handler            = Genode::Signal_handler<Interface>;
		using Signal_context_capability = Genode::Signal_context_capability;

		enum { IPV4_TIME_TO_LIVE          = 64 };
		enum { MAX_FREE_OPS_PER_EMERGENCY = 100 };

		struct Update_domain
		{
			Domain &old_domain;
			Domain &new_domain;

			Update_domain(Domain &old_domain,
			              Domain &new_domain)
			:
				old_domain(old_domain),
				new_domain(new_domain)
			{ }
		};

		Packet_stream_sink                   &_sink;
		Packet_stream_source                 &_source;
		Signal_handler                        _pkt_stream_signal_handler;
		Mac_address                    const  _router_mac;
		Mac_address                    const  _mac;
		Configuration                        *_config_ptr;
		Interface_policy                     &_policy;
		Cached_timer                         &_timer;
		Genode::Allocator                    &_alloc;
		Domain                               *_domain_ptr                { };
		Arp_waiter_list                       _own_arp_waiters           { };
		Arp_waiter_list                       _timed_out_arp_waiters     { };
		Link_list                             _tcp_links                 { };
		Link_list                             _udp_links                 { };
		Link_list                             _icmp_links                { };
		Link_list                             _dissolved_tcp_links       { };
		Link_list                             _dissolved_udp_links       { };
		Link_list                             _dissolved_icmp_links      { };
		Dhcp_allocation_tree                  _dhcp_allocations          { };
		Dhcp_allocation_list                  _released_dhcp_allocations { };
		Genode::Constructible<Dhcp_client>    _dhcp_client               { };
		Interface_list                       &_interfaces;
		Genode::Constructible<Update_domain>  _update_domain             { };
		Interface_link_stats                  _udp_stats                 { };
		Interface_link_stats                  _tcp_stats                 { };
		Interface_link_stats                  _icmp_stats                { };
		Interface_object_stats                _arp_stats                 { };
		Interface_object_stats                _dhcp_stats                { };
		unsigned long                         _dropped_fragm_ipv4        { 0 };

		/*
		 * Noncopyable
		 */
		Interface(Interface const &);
		Interface &operator = (Interface const &);

		[[nodiscard]] Packet_result _new_link(L3_protocol         const  protocol,
		                                     void                *const  prot_base,
		                                     Domain                     &local_domain,
		                                     Link_side_id         const &local_id,
		                                     Port_allocator_guard       *remote_port_alloc_ptr,
		                                     Domain                     &remote_domain,
		                                     Link_side_id         const &remote_id);

		void _destroy_released_dhcp_allocations(Domain &local_domain);

		void _destroy_dhcp_allocation(Dhcp_allocation &allocation,
		                              Domain          &local_domain);

		void _release_dhcp_allocation(Dhcp_allocation &allocation,
		                              Domain          &local_domain);

		void _try_emergency_free_quota();

		void _destroy_timed_out_arp_waiters();

		[[nodiscard]] Packet_result _new_dhcp_allocation(Ethernet_frame &eth,
		                                                Dhcp_packet    &dhcp,
		                                                Dhcp_server    &dhcp_srv,
		                                                Domain         &local_domain);

		void _send_dhcp_reply(Dhcp_server               const &dhcp_srv,
		                      Mac_address               const &eth_dst,
		                      Mac_address               const &client_mac,
		                      Ipv4_address              const &client_ip,
		                      Dhcp_packet::Message_type        msg_type,
		                      Genode::uint32_t                 xid,
		                      Ipv4_address_prefix       const &local_intf);

		void _send_icmp_echo_reply(Ethernet_frame &eth,
		                           Ipv4_packet    &ip,
		                           Icmp_packet    &icmp,
		                           Genode::size_t  icmp_sz,
		                           Size_guard     &size_guard);

		[[nodiscard]] Packet_result _handle_arp(Ethernet_frame       &eth,
		                                       Size_guard           &size_guard,
		                                       Domain               &local_domain);

		void _handle_arp_reply(Ethernet_frame       &eth,
		                       Size_guard           &size_guard,
		                       Arp_packet           &arp,
		                       Domain               &local_domain);

		[[nodiscard]] Packet_result _handle_arp_request(Ethernet_frame       &eth,
		                                               Size_guard           &size_guard,
		                                               Arp_packet           &arp,
		                                               Domain               &local_domain);

		void _send_arp_reply(Ethernet_frame &request_eth,
		                     Arp_packet     &request_arp);

		[[nodiscard]] Packet_result _handle_dhcp_request(Ethernet_frame            &eth,
		                                                Dhcp_server               &dhcp_srv,
		                                                Dhcp_packet               &dhcp,
		                                                Domain                    &local_domain,
		                                                Ipv4_address_prefix const &local_intf);

		[[nodiscard]] Packet_result _handle_ip(Ethernet_frame          &eth,
		                                      Size_guard              &size_guard,
		                                      Packet_descriptor const &pkt,
		                                      Domain                  &local_domain);

		[[nodiscard]] Packet_result _handle_icmp_query(Ethernet_frame          &eth,
		                                Size_guard              &size_guard,
		                                Ipv4_packet             &ip,
		                                Internet_checksum_diff  &ip_icd,
		                                Packet_descriptor const &pkt,
		                                L3_protocol              prot,
		                                void                    *prot_base,
		                                Genode::size_t           prot_size,
		                                Domain                  &local_domain);

		[[nodiscard]] Packet_result _handle_icmp_error(Ethernet_frame          &eth,
		                                              Size_guard              &size_guard,
		                                              Ipv4_packet             &ip,
		                                              Internet_checksum_diff  &ip_icd,
		                                              Packet_descriptor const &pkt,
		                                              Domain                  &local_domain,
		                                              Icmp_packet             &icmp,
		                                              Genode::size_t           icmp_sz);

		[[nodiscard]] Packet_result _handle_icmp(Ethernet_frame            &eth,
		                                        Size_guard                &size_guard,
		                                        Ipv4_packet               &ip,
		                                        Internet_checksum_diff    &ip_icd,
		                                        Packet_descriptor   const &pkt,
		                                        L3_protocol                prot,
		                                        void                      *prot_base,
		                                        Genode::size_t             prot_size,
		                                        Domain                    &local_domain,
		                                        Ipv4_address_prefix const &local_intf);

		[[nodiscard]] Packet_result _adapt_eth(Ethernet_frame          &eth,
		                                      Ipv4_address      const &dst_ip,
		                                      Packet_descriptor const &pkt,
		                                      Domain                  &remote_domain);

		[[nodiscard]] Packet_result _nat_link_and_pass(Ethernet_frame         &eth,
		                                              Size_guard             &size_guard,
		                                              Ipv4_packet            &ip,
		                                              Internet_checksum_diff &ip_icd,
		                                              L3_protocol      const  prot,
		                                              void            *const  prot_base,
		                                              Genode::size_t   const  prot_size,
		                                              Link_side_id     const &local_id,
		                                              Domain                 &local_domain,
		                                              Domain                 &remote_domain);

		void _broadcast_arp_request(Ipv4_address const &src_ip,
		                            Ipv4_address const &dst_ip);

		void _domain_broadcast(Ethernet_frame &eth,
		                       Size_guard     &size_guard,
		                       Domain         &local_domain);

		void _pass_prot_to_domain(Domain                       &domain,
		                          Ethernet_frame               &eth,
		                          Size_guard                   &size_guard,
		                          Ipv4_packet                  &ip,
		                          Internet_checksum_diff const &ip_icd,
		                          L3_protocol            const  prot,
		                          void                  *const  prot_base,
		                          Genode::size_t         const  prot_size);

		void _handle_pkt();

		void _continue_handle_eth(Packet_descriptor const &pkt);

		Ipv4_address const &_router_ip() const;

		void _drop_packet(Packet_descriptor const &pkt, char const *reason);

		[[nodiscard]] Packet_result _handle_eth(void              *const  eth_base,
		                                       Size_guard               &size_guard,
		                                       Packet_descriptor  const &pkt);

		[[nodiscard]] Packet_result _handle_eth(Ethernet_frame           &eth,
		                                       Size_guard               &size_guard,
		                                       Packet_descriptor  const &pkt,
		                                       Domain                   &local_domain);

		void _ack_packet(Packet_descriptor const &pkt);

		void _send_submit_pkt(Genode::Packet_descriptor   &pkt,
		                      void                      * &pkt_base,
		                      Genode::size_t               pkt_size);

		void _update_dhcp_allocations(Domain &old_domain,
                                      Domain &new_domain);

		void _update_own_arp_waiters(Domain &domain);

		void _update_udp_tcp_links(L3_protocol  prot,
		                           Domain      &cln_dom);

		void _update_icmp_links(Domain &cln_dom);

		bool _try_update_link(Link        &link,
		                      Domain      &new_srv_dom,
		                      L3_protocol  prot,
		                      Domain      &cln_dom);

		void _dismiss_link(Link &link);

		void _update_domain_object(Domain &new_domain);

		void _detach_from_domain_raw();

		void _detach_from_domain();

		void _attach_to_domain_raw(Domain &domain);

		void _apply_foreign_arp();

		void _failed_to_send_packet_link();

		void _failed_to_send_packet_submit();

		void _failed_to_send_packet_alloc();

		void _send_icmp_dst_unreachable(Ipv4_address_prefix const &local_intf,
		                                Ethernet_frame      const &req_eth,
		                                Ipv4_packet         const &req_ip,
		                                Icmp_packet::Code   const  code);

		bool link_state() const;

		void _handle_pkt_stream_signal();

		void _reset_and_refetch_domain_ready_state();

		void _refetch_domain_ready_state();

	public:

		Interface(Genode::Entrypoint     &ep,
		          Cached_timer           &timer,
		          Mac_address      const  router_mac,
		          Genode::Allocator      &alloc,
		          Mac_address      const  mac,
		          Configuration          &config,
		          Interface_list         &interfaces,
		          Packet_stream_sink     &sink,
		          Packet_stream_source   &source,
		          Interface_policy       &policy);

		virtual ~Interface();

		void dhcp_allocation_expired(Dhcp_allocation &allocation);

		void send(Genode::size_t pkt_size, auto const &write_to_pkt)
		{
			if (!link_state()) {
				_failed_to_send_packet_link();
				return;
			}
			if (!_source.ready_to_submit()) {
				_failed_to_send_packet_submit();
				return;
			}
			_source.alloc_packet_attempt(pkt_size).with_result(
				[&] (Packet_descriptor pkt)
				{
					void *pkt_base { _source.packet_content(pkt) };
					Size_guard size_guard(pkt_size);
					write_to_pkt(pkt_base, size_guard);
					_send_submit_pkt(pkt, pkt_base, pkt_size);
				},
				[&] (Packet_stream_source::Alloc_packet_error)
				{
					_failed_to_send_packet_alloc();
				}
			);
		}

		void send(Ethernet_frame &eth,
		          Size_guard     &size_guard);

		Link_list &dissolved_links(L3_protocol const protocol);

		Link_list &links(L3_protocol const protocol);

		void cancel_arp_waiting(Arp_waiter &waiter);

		void handle_config_1(Configuration &config);

		void handle_config_2();

		void handle_config_3();

		void attach_to_domain();

		void detach_from_ip_config(Domain &domain);

		void attach_to_ip_config(Domain            &domain,
		                         Ipv4_config const &ip_config);

		void detach_from_remote_ip_config();

		void attach_to_remote_ip_config();

		void attach_to_domain_finish();

		void handle_interface_link_state();

		bool report_empty(Report const &report_cfg) const;

		void report(Genode::Xml_generator &xml, Report const &report_cfg) const;

		void handle_domain_ready_state(bool state);

		void destroy_link(Link &link);

		void with_domain(auto const &domain_fn, auto const &no_domain_fn)
		{
			if (_domain_ptr)
				domain_fn(*_domain_ptr);
			else
				no_domain_fn();
		}

		void with_domain(auto const &fn) { with_domain(fn, []{}); }


		/***************
		 ** Accessors **
		 ***************/

		Configuration       const &config()                    const { return *_config_ptr; }
		Mac_address         const &router_mac()                const { return _router_mac; }
		Mac_address         const &mac()                       const { return _mac; }
		Arp_waiter_list           &own_arp_waiters()                 { return _own_arp_waiters; }
		Arp_waiter_list           &timed_out_arp_waiters()           { return _timed_out_arp_waiters; }
		Signal_context_capability  pkt_stream_signal_handler() const { return _pkt_stream_signal_handler; }
		Interface_link_stats      &udp_stats()                       { return _udp_stats; }
		Interface_link_stats      &tcp_stats()                       { return _tcp_stats; }
		Interface_link_stats      &icmp_stats()                      { return _icmp_stats; }
		Interface_object_stats    &arp_stats()                       { return _arp_stats; }
		Interface_object_stats    &dhcp_stats()                      { return _dhcp_stats; }
		void                       wakeup_source()                   { _source.wakeup(); }
		void                       wakeup_sink()                     { _sink.wakeup(); }
};

#endif /* _INTERFACE_H_ */
