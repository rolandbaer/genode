/*
 * \brief  Configuration of keyboard mode indicators
 * \author Norman Feske
 * \date   2017-10-25
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#ifndef _INPUT__LED_STATE_H_
#define _INPUT__LED_STATE_H_

#include <base/node.h>
#include <base/component.h>

namespace Usb { struct Led_state; }


struct Usb::Led_state
{
	Genode::Env &_env;

	using Name = Genode::String<32>;

	Name const _name;

	Genode::Constructible<Genode::Attached_rom_dataspace> _rom { };

	bool _enabled = false;

	Led_state(Genode::Env &env, Name const &name) : _env(env), _name(name) { }

	void update(Genode::Node const &config, Genode::Signal_context_capability sigh)
	{
		using Attr  = Genode::String<32>;
		using Value = Genode::String<16>;

		Attr  const attr(_name, "_led");
		Value const value = config.attribute_value(attr.string(), Value());

		bool const rom_configured = (value == "rom");

		if (rom_configured && !_rom.constructed()) {
			_rom.construct(_env, _name.string());
			_rom->sigh(sigh);
		}

		if (!rom_configured && _rom.constructed())
			_rom.destruct();

		if (_rom.constructed())
			_rom->update();

		_enabled = _rom.constructed() ? _rom->node().attribute_value("enabled", false)
		                              : config.attribute_value(attr.string(), false);
	}

	bool enabled() const { return _enabled; }
};

#endif /* _INPUT__LED_STATE_H_ */
