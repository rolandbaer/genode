/*
 * \brief  Common types used within the Sculpt manager
 * \author Norman Feske
 * \date   2018-04-30
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _TYPES_H_
#define _TYPES_H_

#include <util/list_model.h>
#include <util/xml_generator.h>
#include <base/env.h>
#include <base/attached_rom_dataspace.h>
#include <platform_session/platform_session.h>
#include <gui_session/gui_session.h>
#include <usb_session/usb_session.h>
#include <log_session/log_session.h>
#include <rm_session/rm_session.h>
#include <timer_session/timer_session.h>
#include <file_system_session/file_system_session.h>
#include <report_session/report_session.h>
#include <block_session/block_session.h>
#include <terminal_session/terminal_session.h>
#include <rom_session/rom_session.h>
#include <rm_session/rm_session.h>
#include <nic_session/nic_session.h>
#include <rtc_session/rtc_session.h>
#include <trace_session/trace_session.h>
#include <io_mem_session/io_mem_session.h>
#include <io_port_session/io_port_session.h>

namespace Sculpt {

	using namespace Genode;

	using Rom_name   = String<64>;
	using Path       = String<128>;
	using Start_name = String<36>;

	using Point = Gui::Point;
	using Rect  = Gui::Rect;
	using Area  = Gui::Area;

	enum Writeable { WRITEABLE, READ_ONLY };

	/*
	 * CPU priorities used within the runtime subsystem
	 */
	enum class Priority {
		BACKGROUND   = -4,
		DEFAULT      = -3,
		NETWORK      = DEFAULT,
		STORAGE      = DEFAULT,
		MULTIMEDIA   = -2,
		DRIVER       = -1,       /* only for latency-critical drivers */
		LEITZENTRALE = MULTIMEDIA,
		NESTED_MAX   = 0,        /* within nested init (inspect) */
	};

	/**
	 * Argument type for controlling the verification of downloads
	 */
	struct Verify { bool value; };

	struct Progress { bool progress; };

	/**
	 * Call 'fn' with the two strings preceeding and following the character 'c'
	 */
	template <typename FN, size_t N>
	static inline auto with_split(String<N> const &s, char c, FN const &fn)
	-> typename Trait::Functor<decltype(&FN::operator())>::Return_type
	{
		char const * const str = s.string();
		for (unsigned i = 0; i < s.length() - 1; i++)
			if (str[i] == c) {
				return fn(String<N>(Cstring(str, i)), String<N>(str + i + 1));
			}
		return fn(String<N>(s), String<N>());
	}
}

#endif /* _TYPES_H_ */
