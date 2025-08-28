/*
 * \brief  Utilities for generating XML
 * \author Norman Feske
 * \date   2018-01-11
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _XML_H_
#define _XML_H_

/* Genode includes */
#include <util/xml_generator.h>
#include <util/callable.h>
#include <base/attached_rom_dataspace.h>
#include <base/log.h>

/* local includes */
#include "types.h"

namespace Sculpt {

	static inline void gen_named_node(Xml_generator &xml,
	                                  char const *type, char const *name, auto const &fn)
	{
		xml.node(type, [&] {
			xml.attribute("name", name);
			fn();
		});
	}

	static inline void gen_named_node(Xml_generator &xml, char const *type, char const *name)
	{
		xml.node(type, [&] { xml.attribute("name", name); });
	}

	static inline void gen_named_node(Xml_generator &xml,
	                                  char const *type, auto const &name, auto const &fn)
	{
		gen_named_node(xml, type, name.string(), fn);
	}

	static inline void gen_named_node(Xml_generator &xml, char const *type, auto const &name)
	{
		gen_named_node(xml, type, name.string());
	}

	template <typename SESSION>
	static inline void gen_service_node(Xml_generator &xml, auto const &fn)
	{
		gen_named_node(xml, "service", SESSION::service_name(), fn);
	}

	template <typename SESSION>
	static inline void gen_parent_service(Xml_generator &xml)
	{
		gen_named_node(xml, "service", SESSION::service_name());
	};

	template <typename SESSION>
	static inline void gen_parent_route(Xml_generator &xml)
	{
		gen_named_node(xml, "service", SESSION::service_name(), [&] {
			xml.node("parent", [&] { }); });
	}

	static inline void gen_parent_rom_route(Xml_generator  &xml,
	                                        Rom_name const &name,
	                                        auto     const &label)
	{
		gen_service_node<Rom_session>(xml, [&] {
			xml.attribute("label_last", name);
			xml.node("parent", [&] {
				xml.attribute("label", label); });
		});
	}

	static inline void gen_parent_rom_route(Xml_generator  &xml,
	                                        Rom_name const &name)
	{
		gen_parent_rom_route(xml, name, name);
	}

	template <typename SESSION>
	static inline void gen_provides(Xml_generator &xml)
	{
		xml.node("provides", [&] {
			gen_named_node(xml, "service", SESSION::service_name()); });
	}

	static inline void gen_common_routes(Xml_generator &xml)
	{
		gen_parent_rom_route(xml, "ld.lib.so");
		gen_parent_route<Cpu_session>    (xml);
		gen_parent_route<Pd_session>     (xml);
		gen_parent_route<Log_session>    (xml);
		gen_parent_route<Timer::Session> (xml);
		gen_parent_route<Report::Session>(xml);
	}

	static inline void gen_common_start_content(Xml_generator   &xml,
	                                            Rom_name  const &name,
	                                            Cap_quota const  caps,
	                                            Ram_quota const  ram,
	                                            Priority  const  priority)
	{
		xml.attribute("name", name);
		xml.attribute("caps", caps.value);
		xml.attribute("priority", (int)priority);
		gen_named_node(xml, "resource", "RAM", [&] {
			xml.attribute("quantum", String<64>(Number_of_bytes(ram.value))); });
	}

	template <typename T>
	static T _attribute_value(Node const &node, char const *attr_name)
	{
		return node.attribute_value(attr_name, T{});
	}

	template <typename T>
	static T _attribute_value(Node const &node, char const *sub_node_type, auto &&... args)
	{
		return node.with_sub_node(sub_node_type,
			[&] (Node const &n) { return _attribute_value<T>(n, args...); },
			[&]                 { return T{}; });
	}

	/**
	 * Query attribute value from XML sub node
	 *
	 * The list of arguments except for the last one refer to XML path into the
	 * XML structure. The last argument denotes the queried attribute name.
	 */
	template <typename T>
	static T query_attribute(Node const &node, auto &&... args)
	{
		return _attribute_value<T>(node, args...);
	}

	struct Rom_data : Noncopyable, Interface
	{
		protected:

			using With_node = Callable<void, Node const &>;

			virtual void _with_node(With_node::Ft const &) const = 0;

		public:

			virtual bool valid() const = 0;

			void with_node(auto const &fn) const { _with_node( With_node::Fn { fn } ); }
	};

	template <typename T>
	class Rom_handler : public Rom_data
	{
		private:

			Attached_rom_dataspace _rom;

			T &_obj;

			void (T::*_member) (Node const &);

			Signal_handler<Rom_handler> _handler;

			void _handle()
			{
				_rom.update();
				(_obj.*_member)(_rom.node());
			}

			void _with_node(With_node::Ft const &fn) const override { fn(_rom.node()); }

		public:

			Rom_handler(Env &env, Session_label const &label,
			            T &obj, void (T::*member)(Node const &))
			:
				_rom(env, label.string()), _obj(obj), _member(member),
				_handler(env.ep(), *this, &Rom_handler::_handle)
			{
				_rom.sigh(_handler);
				_handler.local_submit();
			}

			bool valid() const override { return !_rom.node().has_type("empty"); }
	};
}

#endif /* _XML_H_ */
