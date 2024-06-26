/*
 * \brief  Interface back-end using a NIC session requested by the NIC router
 * \author Martin Stein
 * \date   2016-08-23
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _NIC_CLIENT_H_
#define _NIC_CLIENT_H_

/* Genode includes */
#include <nic_session/connection.h>
#include <nic/packet_allocator.h>

/* local includes */
#include <dictionary.h>
#include <interface.h>
#include <ipv4_address_prefix.h>

namespace Net {

	using Domain_name = Genode::String<160>;
	class Nic_client;
	using Nic_client_dict = Dictionary<Nic_client, Genode::Session_label>;
	class Nic_client_interface_base;
	class Nic_client_interface;
}


class Net::Nic_client : private Nic_client_dict::Element
{
	friend class Genode::Avl_tree<Nic_client>;
	friend class Genode::Avl_node<Nic_client>;
	friend class Genode::Dictionary<Nic_client, Genode::Session_label>;

	private:

		struct Critical { Nic_client_interface *interface_ptr; };

		Genode::Allocator              &_alloc;
		Configuration                  &_config;
		Domain_name              const  _domain;
		Genode::Constructible<Critical> _crit { };

		/*
		 * Noncopyable
		 */
		Nic_client(Nic_client const &);
		Nic_client &operator = (Nic_client const &);

	public:

		Nic_client(Genode::Session_label const &label_arg,
		           Domain_name           const &domain_arg,
		           Genode::Allocator           &alloc,
		           Nic_client_dict             &new_nic_clients,
		           Configuration               &config);

		~Nic_client();

		[[nodiscard]] bool finish_construction(Genode::Env &, Cached_timer &, Interface_list &, Nic_client_dict &);


		/**************
		 ** Acessors **
		 **************/

		Domain_name           const &domain() const { return _domain; }
		Genode::Session_label const &label()  const { return Nic_client_dict::Element::name; }
};


class Net::Nic_client_interface_base : public Interface_policy
{
	private:

		Domain_name           const *_domain_name_ptr;
		Genode::Session_label const  _label;
		bool                  const &_session_link_state;
		bool                         _domain_ready { false };

		/*
		 * Noncopyable
		 */
		Nic_client_interface_base(Nic_client_interface_base const &);
		Nic_client_interface_base &operator = (Nic_client_interface_base const &);


		/***************************
		 ** Net::Interface_policy **
		 ***************************/

		Domain_name determine_domain_name() const override { return *_domain_name_ptr; };
		void handle_config(Configuration const &) override { }
		Genode::Session_label const &label() const override { return _label; }
		void handle_domain_ready_state(bool state) override;
		bool interface_link_state() const override;
		bool report_empty() const override { return true; };
		void report(Genode::Xml_generator &) const override { };

	public:

		Nic_client_interface_base(Domain_name           const &domain_name,
		                          Genode::Session_label const &label,
		                          bool                  const &session_link_state);

		virtual ~Nic_client_interface_base() { }


		/***************
		 ** Accessors **
		 ***************/

		void domain_name(Domain_name const &v) { _domain_name_ptr = &v; }
};


class Net::Nic_client_interface : public Nic_client_interface_base,
                                  public Nic::Packet_allocator,
                                  public Nic::Connection
{
	private:

		enum {
			PKT_SIZE = Nic::Packet_allocator::DEFAULT_PACKET_SIZE,
			BUF_SIZE = Nic::Session::QUEUE_SIZE * PKT_SIZE,
		};

		bool                                         _session_link_state { false };
		Genode::Signal_handler<Nic_client_interface> _session_link_state_handler;
		Net::Interface                               _interface;

		Ipv4_address_prefix _read_interface();

		void _handle_session_link_state();

	public:

		Nic_client_interface(Genode::Env                 &env,
		                     Cached_timer                &timer,
		                     Genode::Allocator           &alloc,
		                     Interface_list              &interfaces,
		                     Configuration               &config,
		                     Domain_name           const &domain_name,
		                     Genode::Session_label const &label);


		/***************
		 ** Accessors **
		 ***************/

		Mac_address const &router_mac() const { return _interface.router_mac(); }
};

#endif /* _NIC_CLIENT_H_ */
