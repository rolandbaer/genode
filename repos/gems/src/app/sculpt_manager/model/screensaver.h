/*
 * \brief  Logic for activating/deactivating the screensaver
 * \author Norman Feske
 * \date   2023-06-27
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _MODEL__SCREENSAVER_H_
#define _MODEL__SCREENSAVER_H_

#include <types.h>

namespace Sculpt { class Screensaver; }


class Sculpt::Screensaver : Noncopyable
{
	public:

		struct Action : Interface
		{
			virtual void screensaver_changed() = 0;
		};

	private:

		Env    &_env;
		Action &_action;

		/* config from outside */
		unsigned _max_seconds_of_inactivity = 60;
		bool     _blank_after_some_time     = true;
		bool     _display_driver_ready      = false;

		/* internally driven state */
		uint64_t _last_activity_ms     = 0;
		bool     _recent_user_activity = true;
		bool     _forced_blanked       = false;

		Timer::Connection _timer { _env };

		Signal_handler<Screensaver> _timer_handler {
			_env.ep(), *this, &Screensaver::_handle_timer };

		void _handle_timer()
		{
			uint64_t const now = _timer.elapsed_ms();

			if (now - _last_activity_ms > _max_seconds_of_inactivity*1000) {
				if (_recent_user_activity) {
					_recent_user_activity = false;
					_action.screensaver_changed();
				}
			}
		}

		void _keep_display_enabled_for_some_time()
		{
			if (!display_enabled())
				return;

			_last_activity_ms = _timer.elapsed_ms();
			_timer.trigger_once(_max_seconds_of_inactivity*1024*1024);
			_recent_user_activity = true;
		}

		/**
		 * Utility for watching the 'active' attribute of a ROM module
		 */
		struct Watched_rom : Noncopyable
		{
			Screensaver &_screensaver;

			Attached_rom_dataspace _rom;

			Signal_handler<Watched_rom> _handler {
				_screensaver._env.ep(), *this, &Watched_rom::_handle };

			Watched_rom(Screensaver &screensaver, char const *label)
			:
				_screensaver(screensaver), _rom(screensaver._env, label)
			{
				_rom.sigh(_handler);
			}

			void _handle()
			{
				_rom.update();
				if (_rom.node().attribute_value("active", false))
					_screensaver._keep_display_enabled_for_some_time();
			}
		};

		Watched_rom _nitpicker_focus { *this, "nitpicker_focus" };
		Watched_rom _nitpicker_hover { *this, "nitpicker_hover" };

		/**
		 * Utility for watching the 'seq_number' attribute of a ROM module
		 */
		struct Watched_rom_seq_number : Noncopyable
		{
			Screensaver &_screensaver;

			Attached_rom_dataspace _rom;

			Signal_handler<Watched_rom_seq_number> _handler {
				_screensaver._env.ep(), *this, &Watched_rom_seq_number::_handle };

			uint64_t _seq_number { 0 };

			Watched_rom_seq_number(Screensaver &screensaver, char const *label)
			:
				_screensaver(screensaver), _rom(screensaver._env, label)
			{
				_rom.sigh(_handler);
			}

			void _handle()
			{
				_rom.update();

				uint64_t seq_number = _rom.node().attribute_value("seq_number", 0ul);
				if (!seq_number) return;

				if (seq_number > _seq_number) {
					_seq_number = seq_number;
					_screensaver._keep_display_enabled_for_some_time();
				}
			}
		};

		/* matches <hover seq_number=.../> */
		Watched_rom_seq_number _menu_hover { *this, "menu_hover" };

	public:

		Screensaver(Env &env, Action &action) : _env(env), _action(action)
		{
			_timer.sigh(_timer_handler);
			_keep_display_enabled_for_some_time();
		}

		/**
		 * Controls the lifetime of the display driver
		 */
		bool display_enabled() const
		{
			if (_forced_blanked)
				return false;

			if (_blank_after_some_time && !_recent_user_activity)
				return false;

			return true;
		}

		/**
		 * Used for enabling screensaver only when the Leitzentrale is visible
		 */
		void blank_after_some_time(bool blank_after_some_time)
		{
			_recent_user_activity = true;

			if (blank_after_some_time != _blank_after_some_time) {
				_blank_after_some_time = blank_after_some_time;
				_keep_display_enabled_for_some_time();
			}
		}

		void display_driver_ready(bool display_driver_ready)
		{
			_display_driver_ready = display_driver_ready;
		}

		/**
		 * User enforces the enabling or disabling of the display (power button)
		 */
		void force_toggle()
		{
			_recent_user_activity = true;

			if (display_enabled() && _display_driver_ready)
				_forced_blanked = true;
			else {
				_forced_blanked = false;
				_keep_display_enabled_for_some_time();
			}

			_action.screensaver_changed();
		}
};

#endif /* _MODEL__SCREENSAVER_H_ */
