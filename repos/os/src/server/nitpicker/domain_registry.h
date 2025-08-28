/*
 * \brief  Domain registry
 * \author Norman Feske
 * \date   2014-06-12
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _DOMAIN_REGISTRY_
#define _DOMAIN_REGISTRY_

#include <types.h>

namespace Nitpicker { class Domain_registry; }


class Nitpicker::Domain_registry
{
	public:

		class Entry : public List<Entry>::Element
		{
			public:

				using Name = String<64>;

				enum class Label   { NO, YES };
				enum class Content { CLIENT, TINTED };
				enum class Hover   { FOCUSED, ALWAYS };
				enum class Focus   { NONE, CLICK, TRANSIENT };

				/**
				 * Origin of the domain's coordiate system
				 */
				enum class Origin { POINTER, TOP_LEFT, TOP_RIGHT,
				                    BOTTOM_LEFT, BOTTOM_RIGHT };

			private:

				Name      _name;
				Color     _color;
				Label     _label;
				Content   _content;
				Hover     _hover;
				Focus     _focus;
				Origin    _origin;
				unsigned  _layer;
				Point     _offset;
				Point     _area;

				friend class Domain_registry;

				Entry(Name const &name, Color color, Label label,
				      Content content, Hover hover, Focus focus,
				      Origin origin, unsigned layer, Point offset, Point area)
				:
					_name(name), _color(color), _label(label),
					_content(content), _hover(hover), _focus(focus),
					_origin(origin), _layer(layer), _offset(offset), _area(area)
				{ }

				Point _corner(Rect const rect) const
				{
					switch (_origin) {
					case Origin::POINTER:      return { 0, 0 };
					case Origin::TOP_LEFT:     return { rect.x1(), rect.y1() };
					case Origin::TOP_RIGHT:    return { rect.x2(), rect.y1() };
					case Origin::BOTTOM_LEFT:  return { rect.x1(), rect.y2() };
					case Origin::BOTTOM_RIGHT: return { rect.x2(), rect.y2() };
					}
					return { 0, 0 };
				}

			public:

				bool has_name(Name const &name) const { return name == _name; }

				Name      name()      const { return _name;    }
				Color     color()     const { return _color;   }
				unsigned  layer()     const { return _layer;   }
				Content   content()   const { return _content; }
				Hover     hover()     const { return _hover;   }

				bool label_visible()   const { return _label   == Label::YES; }
				bool content_client()  const { return _content == Content::CLIENT; }
				bool hover_focused()   const { return _hover   == Hover::FOCUSED; }
				bool hover_always()    const { return _hover   == Hover::ALWAYS; }
				bool focus_click()     const { return _focus   == Focus::CLICK; }
				bool focus_transient() const { return _focus   == Focus::TRANSIENT; }
				bool origin_pointer()  const { return _origin  == Origin::POINTER; }

				Point phys_pos(Point pos, Rect panorama) const
				{
					return pos + _corner(panorama) + _offset;
				}

				Rect screen_rect(Rect const panorama) const
				{
					/* align value to zero or to limit, depending on its sign */
					auto aligned = [&] (unsigned limit, int v)
					{
						return unsigned((v > 0) ? v : max(0, int(limit) + v));
					};

					return { .at   = _offset + panorama.at,
					         .area = { .w = aligned(panorama.w(), _area.x),
					                   .h = aligned(panorama.h(), _area.y) } };
				}
		};

		static Entry::Label _label(Node const &domain)
		{
			using Value = String<32>;
			Value const value = domain.attribute_value("label", Value("yes"));

			if (value == "no")  return Entry::Label::NO;
			if (value == "yes") return Entry::Label::YES;

			warning("invalid value of label attribute in <domain>");
			return Entry::Label::YES;
		}

		static Entry::Content _content(Node const &domain)
		{
			using Value = String<32>;
			Value const value = domain.attribute_value("content", Value("tinted"));

			if (value == "client") return Entry::Content::CLIENT;
			if (value == "tinted") return Entry::Content::TINTED;

			return Entry::Content::TINTED;
		}

		static Entry::Hover _hover(Node const &domain)
		{
			using Value = String<32>;
			Value const value = domain.attribute_value("hover", Value("focused"));

			if (value == "focused") return Entry::Hover::FOCUSED;
			if (value == "always")  return Entry::Hover::ALWAYS;

			warning("invalid value of hover attribute in <domain>");
			return Entry::Hover::FOCUSED;
		}

		static Entry::Focus _focus(Node const &domain)
		{
			using Value = String<32>;
			Value const value = domain.attribute_value("focus", Value("none"));

			if (value == "none")      return Entry::Focus::NONE;
			if (value == "click")     return Entry::Focus::CLICK;
			if (value == "transient") return Entry::Focus::TRANSIENT;

			warning("invalid value of focus attribute in <domain>");
			return Entry::Focus::NONE;
		}

		static Entry::Origin _origin(Node const &domain)
		{
			using Value = String<32>;
			Value const value = domain.attribute_value("origin", Value("top_left"));

			if (value == "top_left")     return Entry::Origin::TOP_LEFT;
			if (value == "top_right")    return Entry::Origin::TOP_RIGHT;
			if (value == "bottom_left")  return Entry::Origin::BOTTOM_LEFT;
			if (value == "bottom_right") return Entry::Origin::BOTTOM_RIGHT;
			if (value == "pointer")      return Entry::Origin::POINTER;

			warning("invalid value of origin attribute in <domain>");
			return Entry::Origin::BOTTOM_LEFT;
		}

		void _insert(Node const &domain)
		{
			Entry::Name const name = domain.attribute_value("name", Entry::Name());

			if (!name.valid()) {
				error("no valid domain name specified");
				return;
			}

			if (lookup(name)) {
				error("domain name \"", name, "\" is not unique");
				return;
			}

			if (!domain.has_attribute("layer")) {
				error("no layer specified for domain \"", name, "\"");
				return;
			}

			unsigned const layer = domain.attribute_value("layer", ~0U);

			Point const offset(domain.attribute_value("xpos", 0),
			                   domain.attribute_value("ypos", 0));

			Point const area(domain.attribute_value("width",  0),
			                 domain.attribute_value("height", 0));

			Color const color = domain.attribute_value("color", white());

			_entries.insert(new (_alloc) Entry(name, color, _label(domain),
			                                   _content(domain), _hover(domain),
			                                   _focus(domain),
			                                   _origin(domain), layer, offset, area));
		}

	private:

		List<Entry> _entries { };
		Allocator  &_alloc;

	public:

		Domain_registry(Allocator &alloc, Node const &config) : _alloc(alloc)
		{
			config.for_each_sub_node("domain", [&] (Node const &domain) {
				_insert(domain); });
		}

		~Domain_registry()
		{
			while (Entry *e = _entries.first()) {
				_entries.remove(e);
				destroy(_alloc, e);
			}
		}

		Entry const *lookup(Entry::Name const &name) const
		{
			for (Entry const *e = _entries.first(); e; e = e->next())
				if (e->has_name(name))
					return e;
			return 0;
		}
};

#endif /* _DOMAIN_REGISTRY_ */
