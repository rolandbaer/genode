/*
 * \brief  Sculpt AHCI-driver management
 * \author Norman Feske
 * \date   2024-03-21
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _DRIVER__AHCI_H_
#define _DRIVER__AHCI_H_

namespace Sculpt { struct Ahci_driver; }


struct Sculpt::Ahci_driver : private Noncopyable
{
	struct Action : Interface
	{
		virtual void handle_ahci_discovered() = 0;
	};

	Env    &_env;
	Action &_action;

	Constructible<Child_state> _ahci { };

	Rom_handler<Ahci_driver> _ports {
		_env, "report -> runtime/ahci/ports", *this, &Ahci_driver::_handle_ports };

	void _handle_ports(Node const &) { _action.handle_ahci_discovered(); }

	Ahci_driver(Env &env, Action &action) : _env(env), _action(action) { }

	void gen_start_node(Xml_generator &xml) const
	{
		if (!_ahci.constructed())
			return;

		xml.node("start", [&] {
			_ahci->gen_start_node_content(xml);
			gen_named_node(xml, "binary", "ahci");
			gen_provides<Block::Session>(xml);
			xml.node("config", [&] {
				xml.attribute("system", "yes");
				xml.node("report", [&] { xml.attribute("ports", "yes"); });
				for (unsigned i = 0; i < 6; i++)
					xml.node("policy", [&] {
						xml.attribute("label",  i);
						xml.attribute("device", i);
						xml.attribute("writeable", "yes"); });
			});
			xml.node("route", [&] {
				gen_parent_route<Platform::Session>(xml);
				gen_parent_rom_route(xml, "ahci");
				gen_parent_rom_route(xml, "system",   "config -> managed/system");
				gen_common_routes(xml);
			});
		});
	};

	void update(Registry<Child_state> &registry, Board_info const &board_info)
	{
		_ahci.conditional(board_info.detected.ahci,
		                 registry, "ahci", Priority::DEFAULT,
		                 Ram_quota { 10*1024*1024 }, Cap_quota { 100 });
	}

	void with_ports(auto const &fn) const
	{
		_ports.with_node([&] (Node const &ports) {
			fn({ .present = _ahci.constructed(), .report = ports }); });
	}
};

#endif /* _DRIVER__AHCI_H_ */
