/*
 * \brief  Registry of ROM modules used as input for the condition
 * \author Norman Feske
 * \date   2015-09-21
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INPUT_ROM_REGISTRY_H_
#define _INPUT_ROM_REGISTRY_H_

/* Genode includes */
#include <util/xml_node.h>
#include <base/attached_rom_dataspace.h>
#include <base/allocator.h>

namespace Rom_filter {

	class Input_rom_registry;

	using Input_rom_name = Genode::String<100>;
	using Input_name     = Genode::String<100>;
	using Input_value    = Genode::String<100>;
	using Node_type_name = Genode::String<80>;
	using Attribute_name = Genode::String<80>;

	using Genode::Signal_context_capability;
	using Genode::Signal_handler;
	using Genode::Xml_node;
	using Genode::Interface;
}


class Rom_filter::Input_rom_registry
{
	public:

		/**
		 * Callback type
		 */
		struct Input_rom_changed_fn : Interface
		{
			virtual void input_rom_changed() = 0;
		};

		/**
		 * Exception type
		 */
		class Nonexistent_input_value { };
		class Nonexistent_input_node  { };

	private:

		class Entry : public Genode::List<Entry>::Element
		{
			private:

				Genode::Env &_env;

				Input_rom_name _name;

				Input_rom_changed_fn &_input_rom_changed_fn;

				Genode::Attached_rom_dataspace _rom_ds { _env, _name.string() };

				Xml_node _top_level { "<empty/>" };

				void _handle_rom_changed()
				{
					_rom_ds.update();
					if (!_rom_ds.valid())
						return;

					_top_level = _rom_ds.xml();

					/* trigger re-evaluation of the inputs */
					_input_rom_changed_fn.input_rom_changed();
				}

				Genode::Signal_handler<Entry> _rom_changed_handler =
					{ _env.ep(), *this, &Entry::_handle_rom_changed };

				/**
				 * Return sub node of 'content' according to the constraints
				 * given by 'path'
				 *
				 * \throw Xml_node::Nonexistent_sub_node
				 */
				static Xml_node _matching_sub_node(Node_type_name type,
				                                   Xml_node const &path,
				                                   Xml_node const &content)
				{
					using Attribute_value = Input_value;

					Xml_node sub_node = content.sub_node(type.string());

					Attribute_name const expected_attr =
						path.attribute_value("attribute", Attribute_name());

					Attribute_value const expected_value =
						path.attribute_value("value", Attribute_value());

					for (;; sub_node = sub_node.next(type.string())) {

						/* attribute remains unspecified -> match */
						if (!expected_attr.valid())
							return sub_node;

						/* value remains unspecified -> match */
						if (!expected_value.valid())
							return sub_node;

						Attribute_value const present_value =
							sub_node.attribute_value(expected_attr.string(),
							                         Attribute_value());

						if (present_value == expected_value)
							return sub_node;
					}

					throw Xml_node::Nonexistent_sub_node();
				}

				/**
				 * Query value from XML-structured ROM content
				 *
				 * \param path     XML node that defines the path to the value
				 * \param content  XML-structured content, to which the path
				 *                 is applied
				 */
				Input_value _query_value(Xml_node path, Xml_node content) const
				{
					for (;;) {

						/*
						 * Take value of an attribute
						 */
						if (path.has_type("attribute")) {

							Attribute_name const attr_name =
								path.attribute_value("name", Attribute_name(""));

							if (!content.has_attribute(attr_name.string()))
								throw Nonexistent_input_value();

							return content.attribute_value(attr_name.string(),
							                               Input_value(""));
						}

						/*
						 * Follow path node
						 */
						if (path.has_type("node")) {

							Node_type_name const sub_node_type =
								path.attribute_value("type", Node_type_name(""));

							try {
								content = _matching_sub_node(sub_node_type, path, content);
								path    = path.sub_node();
							}
							catch (Xml_node::Nonexistent_sub_node) {
								throw Nonexistent_input_value(); }

							continue;
						}

						throw Nonexistent_input_value();
					}
				}

				/**
				 * Return the expected top-level XML node type of a given input
				 */
				static Node_type_name _top_level_node_type(Xml_node input_node)
				{
					Node_type_name const undefined("");

					if (input_node.has_attribute("node"))
						return input_node.attribute_value("node", undefined);

					return input_node.attribute_value("name", undefined);
				}

			public:

				/**
				 * Constructor
				 */
				Entry(Genode::Env &env, Input_rom_name const &name,
				      Input_rom_changed_fn &input_rom_changed_fn)
				:
					_env(env), _name(name),
					_input_rom_changed_fn(input_rom_changed_fn)
				{
					_rom_ds.sigh(_rom_changed_handler);
					try { _top_level = _rom_ds.xml(); }
					catch (...) {}
				}

				Input_rom_name name() const { return _name; }

				/**
				 * Query input value from ROM modules
				 *
				 * \param input_node  XML that describes the path to the
				 *                    input value
				 *
				 * \throw Nonexistent_input_value
				 */
				Input_value query_value(Xml_node input_node) const
				{
					try {
						/*
						 * The creation of the XML node may fail with an
						 * exception if the ROM module contains non-XML data.
						 */
						Xml_node content_node(_top_level);

						/*
						 * Check type of top-level node, query value of the
						 * type name matches.
						 */
						Node_type_name expected = _top_level_node_type(input_node);
						if (content_node.has_type(expected.string()))
							return _query_value(input_node.sub_node(), content_node);

					} catch (...) { }

					if (input_node.has_attribute("default"))
						return input_node.attribute_value("default", Input_value(""));

					throw Nonexistent_input_value();
				}

				Xml_node node() const
				{
					try {
						return Xml_node(_top_level);
					} catch (...) { }

					throw Nonexistent_input_node();
				}
		};

		Genode::Allocator &_alloc;

		Genode::Env &_env;

		Genode::List<Entry> _input_roms { };

		Input_rom_changed_fn &_input_rom_changed_fn;

		/**
		 * Apply functor for each input ROM
		 *
		 * The functor is called with 'Input &' as argument.
		 */
		template <typename FUNC>
		void _for_each_input_rom(FUNC const &func) const
		{
			Entry const *ir   = _input_roms.first();
			Entry const *next = nullptr;
			for (; ir; ir = next) {

				/*
				 * Obtain next element prior calling the functor because
				 * the functor may remove the current element from the list.
				 */
				next = ir->next();

				func(*ir);
			}
		}

		/**
		 * Return ROM name of specified XML node
		 */
		static inline Input_rom_name _input_rom_name(Xml_node input)
		{
			if (input.has_attribute("rom"))
				return input.attribute_value("rom", Input_rom_name(""));

			/*
			 * If no 'rom' attribute was specified, we fall back to use the
			 * name of the input as ROM name.
			 */
			return input.attribute_value("name", Input_rom_name(""));
		}

		/**
		 * Return true if ROM with specified name is known
		 */
		bool _input_rom_exists(Input_rom_name const &name) const
		{
			bool result = false;

			_for_each_input_rom([&] (Entry const &input_rom) {

				if (input_rom.name() == name)
					result = true;
			});

			return result;
		}

		static bool _config_uses_input_rom(Xml_node config,
		                                   Input_rom_name const &name)
		{
			bool result = false;

			config.for_each_sub_node("input", [&] (Xml_node input) {

				if (_input_rom_name(input) == name)
					result = true;
			});

			return result;
		}

		Entry const *_lookup_entry_by_name(Input_rom_name const &name) const
		{
			Entry const *entry = nullptr;

			_for_each_input_rom([&] (Entry const &input_rom) {
				if (input_rom.name() == name)
					entry = &input_rom; });

			return entry;
		}

		/**
		 * \throw Nonexistent_input_value
		 */
		Input_value _query_value_in_roms(Xml_node input_node) const
		{
			Entry const *entry =
				_lookup_entry_by_name(_input_rom_name(input_node));

			try {
				if (entry)
					return entry->query_value(input_node);
			} catch (...) { }

			throw Nonexistent_input_value();
		}

	public:

		/**
		 * Constructor
		 *
		 * \param sigh  signal context capability to install in ROM sessions
		 *              for the inputs
		 */
		Input_rom_registry(Genode::Env &env, Genode::Allocator &alloc,
		                   Input_rom_changed_fn &input_rom_changed_fn)
		:
			_alloc(alloc), _env(env), _input_rom_changed_fn(input_rom_changed_fn)
		{ }

		void update_config(Xml_node config)
		{
			/*
			 * Remove ROMs that are no longer present in the configuration.
			 */
			auto remove_stale_entry = [&] (Entry const &entry) {

				if (_config_uses_input_rom(config, entry.name()))
					return;

				_input_roms.remove(const_cast<Entry *>(&entry));
				Genode::destroy(_alloc, const_cast<Entry *>(&entry));
			};
			_for_each_input_rom(remove_stale_entry);

			/*
			 * Add new appearing ROMs.
			 */
			auto add_new_entry = [&] (Xml_node input) {

				Input_rom_name name = _input_rom_name(input);

				if (_input_rom_exists(name))
					return;

				Entry *entry =
					new (_alloc) Entry(_env, name, _input_rom_changed_fn);

				_input_roms.insert(entry);
			};
			config.for_each_sub_node("input", add_new_entry);
		}

		/**
		 * Lookup value of input with specified name
		 *
		 * \throw Nonexistent_input_value
		 */
		Input_value query_value(Xml_node config, Input_name const &input_name) const
		{
			Input_value input_value;
			bool input_value_defined = false;

			auto handle_input_node = [&] (Xml_node input_node) {

				if (input_node.attribute_value("name", Input_name("")) != input_name)
					return;

				input_value = _query_value_in_roms(input_node);
				input_value_defined = true;
			};

			try {
				config.for_each_sub_node("input", handle_input_node);
			} catch (...) {
				throw Nonexistent_input_value();
			}

			if (!input_value_defined)
				throw Nonexistent_input_value();

			return input_value;
		}

		/**
		 * Generate content of the specifed input
		 *
		 * \throw Nonexistent_input_node
		 */
		void gen_xml(Input_name const &input_name, Genode::Xml_generator &xml, bool skip_toplevel=false)
		{
			Entry const *e = _lookup_entry_by_name(input_name);
			if (!e)
				throw Nonexistent_input_node();

			if (skip_toplevel)
				e->node().with_raw_content([&] (char const *start, Genode::size_t length) {
					xml.append(start, length); });
			else
				e->node().with_raw_node([&] (char const *start, Genode::size_t length) {
					xml.append(start, length); });
		}
};

#endif /* _INPUT_ROM_REGISTRY_H_ */
