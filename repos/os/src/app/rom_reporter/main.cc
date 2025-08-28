/*
 * \brief  Reflect content of ROM module as a report
 * \author Norman Feske
 * \date   2017-12-15
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <base/component.h>
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <os/reporter.h>

namespace Rom_reporter {
	using namespace Genode;
	struct Rom_module;
	struct Main;
}


struct Rom_reporter::Rom_module
{
	Env &_env;

	using Label = String<160>;
	using Type  = Xml_node::Type;

	Label const _label;

	Attached_rom_dataspace _rom { _env, _label.string() };

	struct Reporter : Expanding_reporter
	{
		Type const type;
		Reporter(Env &env, Type const &type, Label const &label)
		: Expanding_reporter(env, type, label), type(type) { }
	};

	Constructible<Reporter> _reporter { };

	Signal_handler<Rom_module> _rom_update_handler {
		_env.ep(), *this, &Rom_module::_handle_rom_update };

	void _handle_rom_update()
	{
		_rom.update();
		Xml_node const &node = _rom.xml();

		if (!_reporter.constructed() || _reporter->type != node.type())
			_reporter.construct(_env, node.type(), _label);

		_reporter->generate([&] (Xml_generator &xml) {
			xml.node_attributes(node);
			if (!xml.append_node_content(node, Xml_generator::Max_depth { 20 }))
				warning("ROM '", _label, "' is too deeply nested");
		});
	}

	Rom_module(Env &env, Label const &label) : _env(env), _label(label)
	{
		_rom.sigh(_rom_update_handler);
		_handle_rom_update();
	}
};


struct Rom_reporter::Main
{
	Env &_env;

	Attached_rom_dataspace _config { _env, "config" };

	Heap _heap { _env.ram(), _env.rm() };

	Main(Genode::Env &env) : _env(env)
	{
		_config.xml().for_each_sub_node("rom", [&] (Xml_node const &rom) {
			new (_heap)
				Rom_module(_env, rom.attribute_value("label",
				                                     Rom_module::Label()));
		});
	}
};


void Component::construct(Genode::Env &env) { static Rom_reporter::Main main(env); }
