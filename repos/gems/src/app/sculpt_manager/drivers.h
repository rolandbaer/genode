/*
 * \brief  Sculpt dynamic drivers management
 * \author Norman Feske
 * \date   2024-03-25
 */

/*
 * Copyright (C) 2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _DRIVERS_H_
#define _DRIVERS_H_

/* local includes */
#include <xml.h>
#include <util/callable.h>
#include <model/child_state.h>
#include <model/board_info.h>
#include <driver/fb.h>

namespace Sculpt { struct Drivers; }


class Sculpt::Drivers : Noncopyable
{
	public:

		struct Action : virtual Fb_driver::Action
		{
			virtual void handle_device_plug_unplug() = 0;
		};

		struct Info : Interface
		{
			virtual void gen_usb_storage_policies(Xml_generator &) const = 0;
		};

		using Children = Registry<Child_state>;

		/**
		 * Argument type for 'with_storage_devices'
		 */
		struct Storage_devices
		{
			struct Driver
			{
				bool const  present;
				Node const &report;

				bool enumerated() const { return !present || !report.has_type("empty"); }
			};

			Driver const &usb, &ahci, &nvme, &mmc;

			bool all_enumerated() const { return usb.enumerated() && ahci.enumerated()
			                                 && nvme.enumerated() &&  mmc.enumerated(); }
		};

	private:

		struct Instance;

		Instance &_instance;

		static Instance &_construct_instance(auto &&...);

		using With_storage_devices = Callable<void, Storage_devices const &>;
		using With_board_info      = Callable<void, Board_info const &>;
		using With_node            = Callable<void, Node const &>;

		void _with(With_storage_devices::Ft    const &) const;
		void _with(With_board_info::Ft         const &) const;
		void _with_platform_info(With_node::Ft const &) const;
		void _with_fb_connectors(With_node::Ft const &) const;

	public:

		Drivers(Env &, Children &, Info const &, Action &);

		void update_usb();
		void update_soc(Board_info::Soc);
		void update_options(Board_info::Options);

		void gen_start_nodes(Xml_generator &) const;

		void with_storage_devices(auto const &fn) const { _with(With_storage_devices::Fn   { fn }); }
		void with_board_info     (auto const &fn) const { _with(With_board_info::Fn        { fn }); }
		void with_platform_info  (auto const &fn) const { _with_platform_info(With_node::Fn { fn }); }
		void with_fb_connectors  (auto const &fn) const { _with_fb_connectors(With_node::Fn { fn }); }

		/* true if hardware is suspend/resume capable */
		bool suspend_supported() const;

		/* true once 'Board_info::Option::suspending' phase is compete */
		bool ready_for_suspend() const;

		struct Resumed { unsigned count; };

		Resumed resumed() const;
};

#endif /* _DRIVERS_H_ */
