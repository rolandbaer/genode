/*
 * \brief   Utilities for creating seL4 kernel objects
 * \author  Norman Feske
 * \date    2015-05-08
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__KERNEL_OBJECT_H_
#define _CORE__INCLUDE__KERNEL_OBJECT_H_

/* Genode includes */
#include <base/exception.h>

/* core includes */
#include <untyped_memory.h>
#include <initial_untyped_pool.h>

namespace Core {

	/**
	 * Index referring to a slot in a CNode
	 */
	struct Cnode_index : Cap_sel
	{
		explicit Cnode_index(uint32_t value) : Cap_sel(value) { }

		Cnode_index(Cap_sel sel) : Cap_sel(sel.value()) { }
	};


	struct Tcb_kobj
	{
		enum { SEL4_TYPE = seL4_TCBObject, SIZE_LOG2 = 12 };
		static char const *name() { return "TCB"; }
	};


	struct Endpoint_kobj
	{
		enum { SEL4_TYPE = seL4_EndpointObject, SIZE_LOG2 = 4 };
		static char const *name() { return "endpoint"; }
	};


	struct Notification_kobj
	{
		enum { SEL4_TYPE = seL4_NotificationObject, SIZE_LOG2 = 4 };
		static char const *name() { return "notification"; }
	};


	struct Cnode_kobj
	{
		enum { SEL4_TYPE = seL4_CapTableObject, SIZE_LOG2 = (CONFIG_WORD_SIZE == 32) ? 4 : 5 };
		static char const *name() { return "cnode"; }
	};


	/**
	 * Create kernel object
	 *
	 * \param KOBJ           kernel-object description
	 * \param service        cap to untyped memory
	 * \param dst_cnode_sel  CNode selector where to store the cap pointing to
	 *                       the new kernel object
	 * \param dst_idx        designated index of cap selector within 'dst_cnode'
	 * \param size_log2      size of kernel object in bits
	 *
	 * \return true on success, otherwise false
	 *
	 * The kernel-object description is a policy type that contains enum
	 * definitions for 'SEL4_TYPE' and 'SIZE_LOG2', and a static function
	 * 'name' that returns the type name as a char const pointer.
	 */
	template <typename KOBJ>
	static bool create(seL4_Untyped const service,
	                   Cap_sel      const dst_cnode_sel,
	                   Cnode_index  const dst_idx,
	                   uint8_t      const size_log2 = 0)
	{
		unsigned   const type        = KOBJ::SEL4_TYPE;
		uint8_t    const size_bits   = size_log2;
		seL4_CNode const root        = dst_cnode_sel.value();
		unsigned   const node_index  = 0;
		unsigned   const node_depth  = 0;
		unsigned   const node_offset = dst_idx.value();
		unsigned   const num_objects = 1;

		int const ret = seL4_Untyped_Retype(service,
		                                    type,
		                                    size_bits,
		                                    root,
		                                    node_index,
		                                    node_depth,
		                                    node_offset,
		                                    num_objects);

		if (ret != seL4_NoError) {
			error("seL4_Untyped_RetypeAtOffset (", KOBJ::name(), ") "
			      "returned ", ret);
		}

		return ret == seL4_NoError;
	}
};

#endif /* _CORE__INCLUDE__KERNEL_OBJECT_H_ */
