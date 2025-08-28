/*
 * \brief  x86_64 DMAR page table definitions
 * \author Johannes Schlatow
 * \date   2023-11-06
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <intel/page_table.h>

void Intel::Level_1_translation_table::generate(Genode::Generator &g, Report_helper &)
{
	for_each_entry([&] (unsigned long i, Descriptor::access_t e) {
		Descriptor::generate_page(i, e, g); });
}


void Intel::Level_2_translation_table::generate(Genode::Generator &g,
                                                Report_helper     &report_helper)
{
	for_each_entry([&] (unsigned long i, Descriptor::access_t e) {
		if (Descriptor::maps_page(e))
			Descriptor::Page::generate_page(i, e, g);
		else
			Descriptor::Table::generate<Entry>(i, e, g, report_helper);
	});
}


void Intel::Level_3_translation_table::generate(Genode::Generator &g,
                                                Report_helper     &report_helper)
{
	for_each_entry([&] (unsigned long i, Descriptor::access_t e) {
		if (Descriptor::maps_page(e))
			Descriptor::Page::generate_page(i, e, g);
		else
			Descriptor::Table::generate<Entry>(i, e, g, report_helper);
	});
}


void Intel::Level_4_translation_table::generate(Genode::Generator &g,
                                                Report_helper     &report_helper)
{
	for_each_entry([&] (unsigned long i, Descriptor::access_t e) {
		Descriptor::generate<Entry>(i, e, g, report_helper);
	});
}
