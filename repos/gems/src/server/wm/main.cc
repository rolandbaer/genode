/*
 * \brief  Window manager
 * \author Norman Feske
 * \date   2014-01-06
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <gui_session/client.h>
#include <framebuffer_session/client.h>
#include <base/component.h>

/* local includes */
#include <gui.h>
#include <report_forwarder.h>
#include <rom_forwarder.h>

namespace Wm { class Main; }


struct Wm::Main : Pointer::Tracker, Gui::Session_component::Action
{
	Env &_env;

	Heap _heap { _env.ram(), _env.rm() };

	/* currently focused window, reported by the layouter */
	Attached_rom_dataspace _focus_rom { _env, "focus" };

	/* resize requests, issued by the layouter */
	Attached_rom_dataspace _resize_request_rom { _env, "resize_request" };

	/* pointer position to be consumed by the layouter */
	Expanding_reporter _pointer_reporter { _env, "pointer", "pointer" };

	/* list of present windows, to be consumed by the layouter */
	Expanding_reporter _window_list_reporter { _env, "window_list", "window_list" };

	Window_registry _window_registry { _heap, _window_list_reporter };

	Gui::Connection _focus_gui_session { _env };

	Gui::Area _screen_area { };

	Signal_handler<Main> _mode_handler {
		_env.ep(), *this, &Main::_handle_mode };

	void _handle_mode()
	{
		_focus_gui_session.with_info_node([&] (Node const &info) {
			_screen_area = Area::from_node(info); });
		_gui_root.propagate_mode_change();
	}

	/**
	 * Gui::Session_component::Action interface
	 */
	void gen_screen_area_info(Generator &g) const override
	{
		g.attribute("width",  _screen_area.w);
		g.attribute("height", _screen_area.h);
	}

	Gui::Root _gui_root { _env, *this, _window_registry, *this, _focus_gui_session };

	static void _with_win_id_from_node(Node const &window, auto const &fn)
	{
		if (window.has_attribute("id"))
			fn(Window_registry::Id { window.attribute_value("id", 0u) });
	}

	void _handle_focus_update()
	{
		_gui_root.revoke_exclusive_input();
		_focus_rom.update();
		bool defined = false;
		_focus_rom.node().with_optional_sub_node("window", [&] (Node const &window) {
			_with_win_id_from_node(window, [&] (Window_registry::Id id) {
				_gui_root.with_gui_session(id, [&] (Capability<Gui::Session> cap) {
					_focus_gui_session.focus(cap);
					defined = true; }); }); });
		if (!defined)
			_focus_gui_session.focus({ });
	}

	Signal_handler<Main> _focus_handler {
		_env.ep(), *this, &Main::_handle_focus_update };

	void _handle_resize_request_update()
	{
		_resize_request_rom.update();
		_resize_request_rom.node().for_each_sub_node("window", [&] (Node const &window) {
			_with_win_id_from_node(window, [&] (Window_registry::Id id) {
				_gui_root.request_resize(id, Area::from_node(window)); }); });
	}

	Signal_handler<Main> _resize_request_handler {
		_env.ep(), *this, &Main::_handle_resize_request_update };

	Report_forwarder _report_forwarder { _env, _heap };
	Rom_forwarder    _rom_forwarder    { _env, _heap };

	Signal_handler<Main> _update_pointer_report_handler {
		_env.ep(), *this, &Main::_handle_update_pointer_report };

	void _handle_update_pointer_report()
	{
		Pointer::Position const pos = _gui_root.last_observed_pointer_pos();

		_pointer_reporter.generate([&] (Generator &g) {
			if (pos.valid) {
				g.attribute("xpos", pos.value.x);
				g.attribute("ypos", pos.value.y);
			}
		});
	}

	/**
	 * Pointer::Tracker interface
	 *
	 * This method is called during the event handling, which may affect
	 * multiple 'Pointer::State' instances. Hence, at call time, not all
	 * pointer states may be up to date. To ensure the consistency of all
	 * pointer states when creating the report, we merely schedule a call
	 * of '_handle_update_pointer_report' that is executed after the event
	 * handling is finished.
	 */
	void update_pointer_report() override
	{
		_update_pointer_report_handler.local_submit();
	}

	Main(Env &env) : _env(env)
	{
		/* initially report an empty window list */
		_window_list_reporter.generate([&] (Generator &) { });

		_focus_rom.sigh(_focus_handler);
		_resize_request_rom.sigh(_resize_request_handler);
		_focus_gui_session.info_sigh(_mode_handler);
		_handle_mode();
	}
};


void Component::construct(Genode::Env &env) { static Wm::Main desktop(env); }
