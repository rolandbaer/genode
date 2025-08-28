/*
 * \brief Translation table definitions for core
 * \author Martin Stein
 * \author Stefan Kalkowski
 * \date 2012-02-22
 */

/*
 * Copyright (C) 2012-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__SPEC__ARM__CORTEX_A9_PAGE_TABLE_H_
#define _CORE__SPEC__ARM__CORTEX_A9_PAGE_TABLE_H_

#include <hw/spec/arm/page_table.h>


constexpr unsigned Hw::Page_table_descriptor::device_tex() {
	return 2; }


constexpr bool Hw::Page_table_descriptor::smp() { return true; }


void Hw::Page_table::table_changed(unsigned long, unsigned long) { }

#endif /* _CORE__SPEC__ARM__CORTEX_A9_PAGE_TABLE_H_ */
