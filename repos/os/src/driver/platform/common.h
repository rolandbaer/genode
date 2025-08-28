/*
 * \brief  Platform driver - compound object for all derivate implementations
 * \author Johannes Schlatow
 * \author Stefan Kalkowski
 * \date   2022-05-10
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _SRC__DRIVERS__PLATFORM__COMMON_H_
#define _SRC__DRIVERS__PLATFORM__COMMON_H_

#include <base/registry.h>

#include <root.h>
#include <device_owner.h>
#include <io_mmu.h>
#include <irq_controller.h>
#include <device_pd.h>

namespace Driver { class Common; };

class Driver::Common : Device_reporter,
                       public Device_owner
{
	private:

		Env                     &_env;
		String<64>               _rom_name;
		Attached_rom_dataspace   _devices_rom   { _env, _rom_name.string() };
		Attached_rom_dataspace   _platform_info { _env, "platform_info"    };
		Heap                     _heap          { _env.ram(), _env.rm()    };
		Sliced_heap              _sliced_heap   { _env.ram(), _env.rm()    };
		Device_model             _devices       { _env, _heap, *this, *this };
		Signal_handler<Common>   _dev_handler   { _env.ep(), *this,
		                                          &Common::_handle_devices };
		Device::Owner            _owner_id      { *this };

		Io_mmu_devices           _io_mmu_devices   { };
		Registry<Io_mmu_factory> _io_mmu_factories { };

		Registry<Irq_controller>         _irq_controller_registry { };
		Registry<Irq_controller_factory> _irq_controller_factories { };

		Driver::Root             _root;

		Constructible<Expanding_reporter> _cfg_reporter { };
		Constructible<Expanding_reporter> _dev_reporter { };
		Constructible<Expanding_reporter> _iommu_reporter { };

		uint64_t _resume_counter { };

		void _handle_devices();
		bool _iommu();

	public:

		Common(Genode::Env                  &env,
		       Attached_rom_dataspace const &config_rom);

		Heap         & heap()    { return _heap;    }
		Device_model & devices() { return _devices; }

		Registry<Io_mmu_factory> & io_mmu_factories() {
			return _io_mmu_factories; }

		Io_mmu_devices & io_mmu_devices() {
			return _io_mmu_devices; }

		Node platform_info() { return _platform_info.node(); }

		Registry<Irq_controller_factory> & irq_controller_factories() {
			return _irq_controller_factories; }

		void announce_service();
		void handle_config(Node const &);
		void acquire_io_mmu_devices();
		void acquire_irq_controller();

		void report_resume();

		/*********************
		 ** Device_reporter **
		 *********************/

		void update_report() override;

		/******************
		 ** Device_owner **
		 ******************/

		void disable_device(Device const &device) override;
};


void Driver::Common::acquire_io_mmu_devices()
{
	auto init_default_mappings = [&] (Io_mmu &io_mmu_dev) {
		_devices.for_each([&] (Device const &device) {
			device.with_optional_io_mmu(io_mmu_dev.name(), [&] () {
				bool has_reserved_mem = false;
				device.for_each_reserved_memory([&] (unsigned,
				                                     Io_mmu::Range range) {
					io_mmu_dev.add_default_range(range, range.start);
					has_reserved_mem = true;
				});

				if (!has_reserved_mem)
					return;

				/* enable default mappings for corresponding pci devices */
				device.for_pci_config([&] (Device::Pci_config const &cfg) {
					io_mmu_dev.enable_default_mappings(
						{cfg.bus_num, cfg.dev_num, cfg.func_num});
				});
			});
		});
	};

	_io_mmu_factories.for_each([&] (Io_mmu_factory &factory) {

		_devices.for_each([&] (Device &dev) {
			if (dev.owner().valid())
				return;

			if (factory.matches(dev)) {
				dev.acquire(*this);
				factory.create(_heap, _io_mmu_devices, dev);

				_io_mmu_devices.for_each([&] (Io_mmu &io_mmu_dev) {
					if (io_mmu_dev.name() == dev.name())
						init_default_mappings(io_mmu_dev);
				});
			}
		});

	});

	/* iterate IOMMU devices and determine address translation mode */
	bool mpu_present    { false };
	bool device_present { false };
	_io_mmu_devices.for_each([&] (Io_mmu const &io_mmu) {
		if (io_mmu.mpu())
			mpu_present = true;
		else
			device_present = true;
	});

	if (device_present && !mpu_present)
		_root.enable_dma_remapping();

	bool kernel_iommu_present { false };
	_io_mmu_devices.for_each([&] (Io_mmu &io_mmu_dev) {
		io_mmu_dev.default_mappings_complete();

		if (io_mmu_dev.name() == "kernel_iommu")
			kernel_iommu_present = true;
	});

	/* if kernel implements iommu, instantiate Kernel_iommu */
	if (_iommu() && !kernel_iommu_present)
		new (_heap) Kernel_iommu(_env, _io_mmu_devices, "kernel_iommu");
}


void Driver::Common::acquire_irq_controller()
{
	_irq_controller_factories.for_each([&] (Irq_controller_factory &factory) {

		_devices.for_each([&] (Device &dev) {
			if (dev.owner().valid())
				return;

			if (factory.matches(dev)) {
				dev.acquire(*this);
				factory.create(_heap, _irq_controller_registry, dev);
			}
		});

	});
}


void Driver::Common::_handle_devices()
{
	_devices_rom.update();
	_devices.update(_devices_rom.node(), _root);
	acquire_io_mmu_devices();
	acquire_irq_controller();
	update_report();
	_root.update_policy();
}


bool Driver::Common::_iommu()
{
	bool iommu = false;
	_platform_info.node().with_optional_sub_node("kernel", [&] (Node const &node) {
		iommu = node.attribute_value("iommu", false); });

	return iommu;
}


void Driver::Common::update_report()
{
	if (_dev_reporter.constructed())
		_dev_reporter->generate([&] (Generator &g) {
			g.attribute("resumed", _resume_counter);
			_devices.generate(g); });
	if (_iommu_reporter.constructed())
		_iommu_reporter->generate([&] (Generator &g) {
			_io_mmu_devices.for_each([&] (Io_mmu &io_mmu) {
				io_mmu.generate(g); }); });
}


void Driver::Common::report_resume()
{
	_resume_counter ++;
	update_report();
}

void Driver::Common::disable_device(Device const &device)
{
	_io_mmu_devices.for_each([&] (Io_mmu &io_mmu) {
		if (io_mmu.name() == device.name())
			destroy(_heap, &io_mmu);
	});
	_irq_controller_registry.for_each([&] (Irq_controller &irq_controller) {
		if (irq_controller.name() == device.name())
			destroy(_heap, &irq_controller);
	});
}


void Driver::Common::handle_config(Node const &config)
{
	config.for_each_sub_node("report", [&] (Node const node) {
		_dev_reporter.conditional(node.attribute_value("devices", false),
		                          _env, "devices", "devices");
		_cfg_reporter.conditional(node.attribute_value("config", false),
		                          _env, "config", "config");
		_iommu_reporter.conditional(node.attribute_value("iommu", false),
		                            _env, "iommu", "iommu");
	});

	_root.update_policy();

	if (_cfg_reporter.constructed())
		_cfg_reporter->generate([&] (Generator &g) {
			g.node_attributes(config);
			(void)g.append_node_content(config, Generator::Max_depth { 20 }); });
}


void Driver::Common::announce_service()
{
	_env.parent().announce(_env.ep().manage(_root));
}


Driver::Common::Common(Genode::Env                  &env,
                       Attached_rom_dataspace const &config_rom)
:
	_env(env),
	_rom_name(config_rom.node().attribute_value("devices_rom",
	                                            String<64>("devices"))),
	_root(_env, _sliced_heap, config_rom, _devices, _io_mmu_devices,
	      _irq_controller_registry, _iommu())
{
	_devices_rom.sigh(_dev_handler);
	_handle_devices();
}

#endif /* _SRC__DRIVERS__PLATFORM__COMMON_H_ */
