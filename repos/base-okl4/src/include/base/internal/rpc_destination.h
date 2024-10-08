/*
 * \brief  RPC destination type
 * \author Norman Feske
 * \date   2016-03-11
 */

/*
 * Copyright (C) 2016-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__BASE__INTERNAL__RPC_DESTINATION_H_
#define _INCLUDE__BASE__INTERNAL__RPC_DESTINATION_H_

/* base-internal includes */
#include <base/internal/okl4.h>

namespace Genode {

	using Rpc_destination = Okl4::L4_ThreadId_t;

	static inline Rpc_destination invalid_rpc_destination()
	{
		Okl4::L4_ThreadId_t tid;
		tid.raw = 0;
		return tid;
	}

	static void print(Output &out, Rpc_destination const &dst)
	{
		Genode::print(out, "tid=", Hex(dst.raw));
	}
}

#endif /* _INCLUDE__BASE__INTERNAL__RPC_DESTINATION_H_ */
