/*
 * \brief  Capture test
 * \author Norman Feske
 * \date   2020-06-26
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/env.h>
#include <base/component.h>
#include <base/log.h>
#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <base/registry.h>
#include <gui_session/connection.h>
#include <capture_session/connection.h>
#include <timer_session/connection.h>

namespace Test {

	using namespace Genode;

	struct Main;
}


struct Test::Main
{
	Env &_env;

	using Pixel          = Capture::Pixel;
	using Affected_rects = Capture::Session::Affected_rects;

	Attached_rom_dataspace _config { _env, "config" };

	Heap _heap { _env.ram(), _env.rm() };

	static Gui::Area _area_from_node(Node const &node, Gui::Area default_area)
	{
		return Gui::Area(node.attribute_value("width",  default_area.w),
		                 node.attribute_value("height", default_area.h));
	}

	struct Output
	{
		struct Invalid_config : Exception { };

		Env &_env;

		Allocator &_alloc;

		Gui::Connection _gui { _env, "" };

		Framebuffer::Mode const _mode;

		void _validate_mode() const
		{
			if (_mode.area.count() == 0) {
				error("invalid or missing 'width' and 'height' config attributes");
				throw Invalid_config();
			}
		}

		bool _gui_buffer_init = ( _validate_mode(), _gui.buffer(_mode), true );

		Attached_dataspace _fb_ds { _env.rm(), _gui.framebuffer.dataspace() };

		Registry<Registered<Gui::Top_level_view>> _views { };

		Output(Env &env, Allocator &alloc, Node const &config)
		:
			_env(env), _alloc(alloc),
			_mode({ .area = _area_from_node(config, Area { }), .alpha = false })
		{
			auto view_rect = [&] (Node const &node)
			{
				return Gui::Rect(Gui::Point::from_node(node),
				                 _area_from_node(node, _mode.area));
			};

			config.for_each_sub_node("view", [&] (Node const &node) {
				new (_alloc)
					Registered<Gui::Top_level_view>(_views, _gui, view_rect(node)); });
		}

		~Output()
		{
			_views.for_each([&] (Registered<Gui::Top_level_view> &view) {
				destroy(_alloc, &view); });
		}

		template <typename FN>
		void with_surface(FN const &fn)
		{
			Surface<Pixel> surface(_fb_ds.local_addr<Pixel>(), _mode.area);

			fn(surface);
		}
	};

	Constructible<Output> _output { };

	struct Capture_input
	{
		Env &_env;

		Gui::Area const _area;

		Capture::Connection _capture { _env, "" };

		bool _capture_buffer_init = (
			_capture.buffer({ .px       = _area,
			                  .mm       = { },
			                  .viewport = { { }, _area } }), true );

		Attached_dataspace _capture_ds { _env.rm(), _capture.dataspace() };

		Texture<Pixel> const _texture { _capture_ds.local_addr<Pixel>(), nullptr, _area };

		Gui::Point _at { };

		Capture_input(Env &env, Gui::Area area, Node const &config)
		:
			_env(env), _area(area), _at(Gui::Point::from_node(config))
		{ }

		Affected_rects capture() { return _capture.capture_at(_at); }

		template <typename FN>
		void with_texture(FN const &fn) const
		{
			fn(_texture);
		}
	};

	Constructible<Capture_input> _capture_input { };

	/*
	 * Periodic update
	 */

	Timer::Connection _timer { _env };

	Signal_handler<Main> _timer_handler { _env.ep(), *this, &Main::_handle_timer };

	void _handle_timer()
	{
		if (!_capture_input.constructed() || !_output.constructed())
			return;

		_capture_input->with_texture([&] (Texture<Pixel> const &texture) {

			_output->with_surface([&] (Surface<Pixel> &surface) {

				Affected_rects const affected = _capture_input->capture();

				affected.for_each_rect([&] (Gui::Rect const rect) {

					surface.clip(rect);

					Blit_painter::paint(surface, texture, Gui::Point(0, 0));
				});

				affected.for_each_rect([&] (Gui::Rect const rect) {
					_output->_gui.framebuffer.refresh(rect); });
			});
		});

	}

	void _handle_config()
	{
		_config.update();

		Node const &config = _config.node();

		_output.construct(_env, _heap, config);
		_capture_input.construct(_env, _output->_mode.area, config);

		unsigned long const period_ms = config.attribute_value("period_ms", 0U);

		if (period_ms == 0)
			warning("missing or invalid 'period_ms' config attribute");

		_timer.trigger_periodic(1000*period_ms);
	}

	Signal_handler<Main> _config_handler { _env.ep(), *this, &Main::_handle_config };

	Main(Env &env) : _env(env)
	{
		_timer.sigh(_timer_handler);
		_config.sigh(_config_handler);

		_handle_config();
	}
};


void Component::construct(Genode::Env &env) { static Test::Main main(env); }
