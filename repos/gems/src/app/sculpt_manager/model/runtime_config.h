/*
 * \brief  Cached information of the current runtime configuration
 * \author Norman Feske
 * \date   2019-02-22
 */

/*
 * Copyright (C) 2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _MODEL__RUNTIME_CONFIG_H_
#define _MODEL__RUNTIME_CONFIG_H_

/* Genode includes */
#include <util/list_model.h>
#include <util/dictionary.h>
#include <base/registry.h>
#include <dialog/types.h>

/* local includes */
#include <types.h>
#include <model/service.h>

namespace Sculpt { class Runtime_config; }


class Sculpt::Runtime_config
{
	private:

		Allocator &_alloc;

		/**
		 * Return target name of route specified by <service> node
		 *
		 * For a route to another child, the target name is the child name.
		 * For a route to the parent, the target name expresses a role of
		 * the parent:
		 *
		 * - 'hardware' provides access to hardware
		 * - 'config' allows the change of the systems configuration
		 * - 'info' reveals system information
		 * - 'GUI' connects to the nitpicker GUI server
		 */
		static Start_name _to_name(Node const &node)
		{
			Start_name result { };
			node.with_optional_sub_node("child", [&] (Node const &child) {
				result = child.attribute_value("name", Start_name()); });

			if (result.valid())
				return result;

			node.with_optional_sub_node("parent", [&] (Node const &parent) {

				Service::Type_name const service =
					node.attribute_value("name", Service::Type_name());

					Service::Label const dst_label =
					parent.attribute_value("label", Service::Label());

				bool const ignored_service = (service == "CPU")
				                          || (service == "PD")
				                          || (service == "Report")
				                          || (service == "Timer")
				                          || (service == "LOG");
				if (ignored_service)
					return;

				bool const hardware = (service == "Platform")
				                   || (service == "IO_PORT")
				                   || (service == "IO_MEM")
				                   || (service == "Rtc")
				                   || (service == "IRQ")
				                   || (service == "TRACE");
				if (hardware) {
					result = "hardware";
					return;
				}

				bool const usb = (service == "Usb");
				if (usb) {
					result = "usb";
					return;
				}

				if (service == "ROM") {

					/*
					 * ROM sessions for plain binaries (e.g, as requested by
					 * the sculpt-managed inspect or part_block instances) are
					 * not interesting for the graph. Non-sculpt-managed
					 * subsystems can only be connected to the few ROMs
					 * whitelisted in the 'Parent_services' definition below.
					 */
					bool const interesting_rom =
						dst_label.valid() &&
						(strcmp("config", dst_label.string(), 5) == 0 ||
						 dst_label == "platform_info" ||
						 dst_label == "capslock");

					if (interesting_rom) {
						result = "info";
						return;
					}
				}

				if (service == "File_system") {

					if (dst_label == "config") {
						result = "config";
						return;
					}

					if (dst_label == "fonts" || dst_label == "report") {
						result = "info";
						return;
					}
				}

				bool const gui = (service == "Gui")
				              || (service == "Event")
				              || (service == "Capture");
				if (gui) {
					result = "GUI";
					return;
				}
			});

			return result;
		}

		/**
		 * Return component name targeted by the first route of the start node
		 */
		static Start_name _primary_dependency(Node const &start)
		{
			Start_name result { };
			start.with_optional_sub_node("route", [&] (Node const &route) {
				route.with_optional_sub_node("service", [&] (Node const &service) {
					result = _to_name(service); }); });

			return result;
		}

		struct Child_service : Service, List_model<Child_service>::Element
		{
			static Service::Type type_from_node(Node const &service)
			{
				auto const name = service.attribute_value("name", Service::Type_name());
				for (unsigned i = 0; i < (unsigned)Type::UNDEFINED; i++) {
					Type const t = (Type)i;
					if (name == Service::name_attr(t))
						return t;
				}

				return Type::UNDEFINED;
			}

			Child_service(Start_name server, Node const &provides)
			: Service(server, type_from_node(provides), { }) { }

			static bool type_matches(Node const &node)
			{
				return type_from_node(node) != Service::Type::UNDEFINED;
			}

			bool matches(Node const &node) const
			{
				return type_from_node(node) == type;
			}
		};

		/*
		 * Data structure to associate dialog widget IDs with component names.
		 */
		struct Graph_id;

		using Graph_ids = Dictionary<Graph_id, Start_name>;

		struct Graph_id : Graph_ids::Element, Dialog::Id
		{
			Graph_id(Graph_ids &dict, Start_name const &name, Dialog::Id const &id)
			:
				Graph_ids::Element(dict, name), Dialog::Id(id)
			{ }
		};

		Graph_ids _graph_ids { };

		unsigned _graph_id_count = 0;

	public:

		struct Component : List_model<Component>::Element
		{
			Start_name const name;

			Graph_id const graph_id;

			Start_name primary_dependency { };

			struct Dep : List_model<Dep>::Element
			{
				Start_name const to_name;

				Dep(Start_name to_name) : to_name(to_name) { }

				bool matches(Node const &node) const
				{
					return _to_name(node) == to_name;
				}

				static bool type_matches(Node const &node)
				{
					return _to_name(node).valid();
				}
			};

			/* dependencies on other child components */
			List_model<Dep> deps { };

			void for_each_secondary_dep(auto const &fn) const
			{
				deps.for_each([&] (Dep const &dep) {
					if (dep.to_name != primary_dependency)
						fn(dep.to_name); });
			}

			List_model<Child_service> _child_services { };

			Component(Start_name const &name, Graph_ids &graph_ids, Dialog::Id const &id)
			:
				name(name), graph_id(graph_ids, name, id)
			{ }

			void for_each_service(auto const &fn) const
			{
				_child_services.for_each(fn);
			}

			void update_route_from_node(Allocator &alloc, Node const &route)
			{
				deps.update_from_node(route,

					/* create */
					[&] (Node const &node) -> Dep & {
						return *new (alloc) Dep(_to_name(node)); },

					/* destroy */
					[&] (Dep &e) { destroy(alloc, &e); },

					/* update */
					[&] (Dep &, Node const &) { }
				);
			}

			void update_provides_from_node(Allocator &alloc, Node const &provides)
			{
				_child_services.update_from_node(provides,

					/* create */
					[&] (Node const &node) -> Child_service & {
						return *new (alloc)
							Child_service(name, node); },

					/* destroy */
					[&] (Child_service &e) { destroy(alloc, &e); },

					/* update */
					[&] (Child_service &, Node const &) { }
				);
			}

			void update_from_node(Allocator &alloc, Node const &node)
			{
				primary_dependency = _primary_dependency(node);

				node.with_optional_sub_node("route", [&] (Node const &route) {
					update_route_from_node(alloc, route); });

				node.with_optional_sub_node("provides", [&] (Node const &provides) {
					update_provides_from_node(alloc, provides); });
			}

			bool matches(Node const &node) const
			{
				return node.attribute_value("name", Start_name()) == name;
			}

			static bool type_matches(Node const &node)
			{
				return node.has_type("start");
			}
		};

	private:

		List_model<Component> _components { };

		struct Parent_services
		{
			using Parent_service = Registered_no_delete<Service>;
			using Type = Service::Type;

			Registry<Parent_service> _r { };

			Parent_service const
				_focus     { _r, Type::GUI,         "keyboard focus",                 "focus" },
				_backdrop  { _r, Type::GUI,         "desktop background",             "backdrop" },
				_lockscreen{ _r, Type::GUI,         "desktop lock screen",            "lock_screen" },
				_nitpicker { _r, Type::GUI,         "system GUI server" },
				_lz_event  { _r, Type::EVENT,       "management GUI events",          "leitzentrale" },
				_event     { _r, Type::EVENT,       "filtered input events" },
				_lz_capture{ _r, Type::CAPTURE,     "management GUI",                 "leitzentrale" },
				_capture   { _r, Type::CAPTURE,     "system GUI",                     "global" },
				_config_fs { _r, Type::FILE_SYSTEM, "writeable system configuration", "config", Service::Match_label::FS },
				_report_fs { _r, Type::FILE_SYSTEM, "read-only system reports",       "report", Service::Match_label::FS },
				_capslock  { _r, Type::ROM,         "global capslock state",          "capslock" },
				_vimrc     { _r, Type::ROM,         "default vim configuration",      "config -> vimrc" },
				_fonts     { _r, Type::ROM,         "system font configuration",      "config -> managed/fonts" },
				_pf_info   { _r, Type::ROM,         "platform information",           "platform_info" },
				_bld_info  { _r, Type::ROM,         "build information",              "build_info" },
				_system    { _r, Type::ROM,         "system status",                  "config -> managed/system" },
				_report    { _r, Type::REPORT,      "system reports" },
				_shape     { _r, Type::REPORT,      "pointer shape",    "shape",     Service::Match_label::LAST },
				_copy      { _r, Type::REPORT,      "global clipboard", "clipboard", Service::Match_label::LAST },
				_paste     { _r, Type::ROM,         "global clipboard", "clipboard", Service::Match_label::LAST },
				_rm        { _r, Type::RM,          "custom virtual memory objects" },
				_io_mem    { _r, Type::IO_MEM,      "raw hardware access" },
				_io_port   { _r, Type::IO_PORT,     "raw hardware access" },
				_irq       { _r, Type::IRQ,         "raw hardware access" },
				_block     { _r, Type::BLOCK,       "direct block-device access" },
				_pci_wifi  { _r, Type::PLATFORM,    "wifi hardware",    "wifi" },
				_pci_net   { _r, Type::PLATFORM,    "network hardware", "nic" },
				_pci_audio { _r, Type::PLATFORM,    "audio hardware",   "audio" },
				_pci_acpi  { _r, Type::PLATFORM,    "ACPI",             "acpica" },
				_hw_gpu    { _r, Type::PLATFORM,    "GPU hardware",     "gpu" },
				_pin_state { _r, Type::PIN_STATE,   "GPIO pin state" },
				_pin_ctrl  { _r, Type::PIN_CONTROL, "GPIO pin control" },
				_trace_all { _r, Type::TRACE,       "system",      "global" },
				_trace_rt  { _r, Type::TRACE,       "deployment",  "runtime" },
				_trace     { _r, Type::TRACE,       "component" },
				_vm        { _r, Type::VM,          "virtualization hardware" },
				_pd        { _r, Type::PD,          "system PD service" },
				_monitor   { _r, Type::TERMINAL,    "debug monitor" };

			void for_each(auto const &fn) const { _r.for_each(fn); }

		} _parent_services { };

		Service const _used_fs_service { "default_fs_rw",
		                                 Service::Type::FILE_SYSTEM,
		                                 { }, "used file system" };

	public:

		Runtime_config(Allocator &alloc) : _alloc(alloc) { }

		void update_from_node(Node const &config)
		{
			_components.update_from_node(config,

				/* create */
				[&] (Node const &node) -> Component & {
					return *new (_alloc)
						Component(node.attribute_value("name", Start_name()),
						          _graph_ids,
						          Dialog::Id { _graph_id_count++ });
				},

				/* destroy */
				[&] (Component &e) {

					/* flush list models */
					e.update_route_from_node   (_alloc, Node());
					e.update_provides_from_node(_alloc, Node());

					destroy(_alloc, &e);
				},

				/* update */
				[&] (Component &e, Node const &node) {
					e.update_from_node(_alloc, node); }
			);
		}

		bool present_in_runtime(Start_name const &name) const
		{
			bool result = false;
			_components.for_each([&] (Component const &component) {
				if (component.name == name)
					result = true; });
			return result;
		}

		void with_start_name(Dialog::Id const &id, auto const &fn) const
		{
			_components.for_each([&] (Component const &component) {
				if (component.graph_id == id)
					fn(component.name); });
		}

		void with_graph_id(Start_name const &name, auto const &fn) const
		{
			_graph_ids.with_element(name,
				[&] (Graph_id const &id) { fn(id); },
				[&] { });
		}

		void for_each_component(auto const &fn) const { _components.for_each(fn); }

		/**
		 * Call 'fn' with the name of each dependency of component 'name'
		 */
		void for_each_dependency(Start_name const &name, auto const &fn) const
		{
			_components.for_each([&] (Component const &component) {
				if (component.name == name) {
					component.deps.for_each([&] (Component::Dep const &dep) {
						fn(dep.to_name); }); } });
		}

		void for_each_service(auto const &fn) const
		{
			_parent_services.for_each(fn);

			fn(_used_fs_service);

			_components.for_each([&] (Component const &component) {
				component.for_each_service(fn); });
		}

		unsigned num_service_options(Service::Type const type) const
		{
			unsigned count = 0;
			for_each_service([&] (Service const &service) {
				if (service.type == type)
					count++; });
			return count;
		}
};

#endif /* _MODEL__RUNTIME_CONFIG_H_ */
