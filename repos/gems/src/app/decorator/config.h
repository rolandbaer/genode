/*
 * \brief  Decorator configuration handling
 * \author Norman Feske
 * \date   2015-09-17
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

/* Genode includes */
#include <util/reconstructible.h>
#include <os/session_policy.h>
#include <os/buffered_xml.h>
#include <util/color.h>

/* decorator includes */
#include <decorator/types.h>

namespace Decorator {

	class Config;
	using Window_title = Genode::String<200>;

	using Genode::Allocator;
	using Genode::Reconstructible;
}


class Decorator::Config
{
	public:

		class Window_control
		{
			public:

				enum Type { TYPE_CLOSER, TYPE_TITLE, TYPE_MAXIMIZER,
				            TYPE_MINIMIZER, TYPE_UNMAXIMIZER, TYPE_UNDEFINED };

				enum Align { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

			private:

				Type  _type  = TYPE_UNDEFINED;
				Align _align = ALIGN_CENTER;

			public:

				Window_control() { }

				Window_control(Type type, Align align)
				: _type(type), _align(align) { }

				Type  type()  const { return _type; }
				Align align() const { return _align; }

				static char const *type_name(Type type)
				{
					switch (type) {
					case TYPE_CLOSER:      return "closer";
					case TYPE_TITLE:       return "title";
					case TYPE_MAXIMIZER:   return "maximizer";
					case TYPE_MINIMIZER:   return "minimizer";
					case TYPE_UNMAXIMIZER: return "unmaximizer";
					case TYPE_UNDEFINED:   return "undefined";
					};
					return "";
				}

				bool operator != (Window_control const &other) const
				{
					return _type != other._type || _align != other._align;
				}
		};

	private:

		/**
		 * Noncopyable
		 */
		Config(Config const &);
		Config & operator = (Config const &);

		Genode::Allocator &_alloc;

		Reconstructible<Genode::Buffered_xml> _buffered_config;

		/**
		 * Maximum number of configured window controls
		 */
		enum { MAX_WINDOW_CONTROLS = 10 };

		/**
		 * Array of window elements
		 */
		Window_control *_window_controls[MAX_WINDOW_CONTROLS];

		unsigned _num_window_controls = 0;

		void _reset_window_controls()
		{
			for (unsigned i = 0; i < MAX_WINDOW_CONTROLS; i++) {
				if (_window_controls[i]) {
					Genode::destroy(_alloc, _window_controls[i]);
					_window_controls[i] = nullptr;
				}
			}
			_num_window_controls = 0;
		}

	public:

		Config(Genode::Allocator &alloc, Xml_node config)
		:
			_alloc(alloc), _buffered_config(_alloc, config)
		{
			_reset_window_controls();
		}

		/**
		 * Exception type
		 */
		class Index_out_of_range { };

		/**
		 * Return information about the Nth window control
		 *
		 * The index 'n' denotes the position of the window control from left
		 * to right.
		 *
		 * \throw Index_out_of_range
		 */
		Window_control window_control(unsigned n) const
		{
			/* return title of no controls are configured */
			if (_num_window_controls == 0 && n == 0)
				return Window_control(Window_control::TYPE_TITLE,
				                      Window_control::ALIGN_CENTER);

			if (n >= MAX_WINDOW_CONTROLS || !_window_controls[n])
				throw Index_out_of_range();

			return *_window_controls[n];
		}

		unsigned num_window_controls() const
		{
			/*
			 * We always report at least one window control. Even if none
			 * was configured, we present a title.
			 */
			return Genode::max(_num_window_controls, 1U);
		}

		/**
		 * Return the base color of the window with the specified title
		 */
		Color base_color(Window_title const &title) const
		{
			Color const default_color = Color::rgb(68, 75, 95);
			return with_matching_policy(title, _buffered_config->xml,
				[&] (Xml_node const &policy) {
					return policy.attribute_value("color", default_color); },
				[&] {
					return default_color;
				});
		}

		/**
		 * Return gradient intensity in percent
		 */
		unsigned gradient_percent(Window_title const &title) const
		{
			unsigned const default_gradient =
				_buffered_config->xml.attribute_value("gradient", 32U);

			return with_matching_policy(title, _buffered_config->xml,
				[&] (Xml_node const &policy) {
					return policy.attribute_value("gradient", default_gradient); },
				[&] {
					return default_gradient;
				});
		}

		/**
		 * Update the internally cached configuration state
		 */
		void update(Xml_node config)
		{
			_buffered_config.construct(_alloc, config);

			_reset_window_controls();

			auto configure_window_control = [&] (Xml_node const &node)
			{
				if (_num_window_controls >= MAX_WINDOW_CONTROLS) {
					Genode::warning("number of configured window controls exceeds maximum");
					return;
				}

				Window_control::Type  type  = Window_control::TYPE_UNDEFINED;
				Window_control::Align align = Window_control::ALIGN_CENTER;

				if (node.has_type("title"))     type = Window_control::TYPE_TITLE;
				if (node.has_type("closer"))    type = Window_control::TYPE_CLOSER;
				if (node.has_type("maximizer")) type = Window_control::TYPE_MAXIMIZER;
				if (node.has_type("minimizer")) type = Window_control::TYPE_MINIMIZER;

				auto const align_attr = node.attribute_value("align", Genode::String<16>());
				if (align_attr == "left")  align = Window_control::ALIGN_LEFT;
				if (align_attr == "right") align = Window_control::ALIGN_RIGHT;

				_window_controls[_num_window_controls++] =
					new (_alloc) Window_control(type, align);
			};

			config.with_optional_sub_node("controls", [&] (Xml_node const &controls) {
				controls.for_each_sub_node([&] (Xml_node const &node) {
					configure_window_control(node); }); });
		}
};

#endif /* _CONFIG_H_ */
