/*
 * \brief  Linux test driver
 * \author Alexander Boettcher
 * \date   2022-07-01
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>

/* emulation includes */
#include <lx_emul/init.h>
#include <lx_kit/env.h>


namespace Test {
	using namespace Genode;
	struct Driver;
}


unsigned long long tsc_freq_khz;


struct Test::Driver
{
	Env      &env;

	Signal_handler<Driver> _signal_handler {
		env.ep(), *this, &Driver::_handle_signal };

	void _handle_signal()
	{
		Lx_kit::env().scheduler.execute();
	}

	Driver(Env &env) : env(env)
	{
		Lx_kit::initialize(env, _signal_handler);

		tsc_freq_khz = 0ULL;
		Attached_rom_dataspace info(env, "platform_info");
		info.node().with_optional_sub_node("hardware", [&] (Node const &hardware) {
			hardware.with_optional_sub_node("tsc", [&] (Node const &tsc) {
				tsc_freq_khz = tsc.attribute_value("freq_khz", 0ULL); }); });
	}

	void start()
	{
		log("--- Test driver started ---");

		lx_emul_start_kernel(nullptr);
	}
};


static Test::Driver &driver(Genode::Env & env)
{
	static Test::Driver driver(env);
	return driver;
}


void Component::construct(Genode::Env &env)
{
	driver(env).start();
}
