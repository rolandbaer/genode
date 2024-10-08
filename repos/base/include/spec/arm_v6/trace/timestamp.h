/*
 * \brief  Trace timestamp
 * \author Stefan Kalkowski
 * \date   2013-08-20
 *
 * Serialized reading of performance counter on ARM.
 */

/*
 * Copyright (C) 2013-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__SPEC__ARM_V6__TRACE__TIMESTAMP_H_
#define _INCLUDE__SPEC__ARM_V6__TRACE__TIMESTAMP_H_

#include <base/fixed_stdint.h>

namespace Genode { namespace Trace {

	using Timestamp = uint32_t;

	inline Timestamp timestamp()
	{
		uint32_t t;
		asm volatile("mrc p15, 0, %0, c15, c12, 1" : "=r"(t));
		return t;
	}
} }

#endif /* _INCLUDE__SPEC__ARM_V6__TRACE__TIMESTAMP_H_ */
