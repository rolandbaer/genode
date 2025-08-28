/*
 * \brief  Types used by VFS
 * \author Norman Feske
 * \date   2014-04-07
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__TYPES_H_
#define _INCLUDE__VFS__TYPES_H_

#include <util/list.h>
#include <util/misc_math.h>
#include <util/string.h>
#include <base/node.h>
#include <base/env.h>
#include <base/signal.h>
#include <base/allocator.h>
#include <dataspace/client.h>
#include <os/path.h>

namespace Vfs {

	enum { MAX_PATH_LEN = 512 };

	using Genode::Ram_dataspace_capability;
	using Genode::Dataspace_capability;
	using Genode::Dataspace_client;
	using Genode::min;
	using Genode::ascii_to;
	using Genode::copy_cstring;
	using Genode::strcmp;
	using Genode::strlen;
	using file_offset = long long;
	using Genode::memcpy;
	using Genode::memset;
	using file_size = unsigned long long;
	using Genode::List;
	using Genode::Node;
	using Genode::Signal_context_capability;
	using Genode::static_cap_cast;
	using Genode::Interface;
	using Genode::String;
	using Genode::size_t;
	using Genode::Byte_range_ptr;
	using Genode::Const_byte_range_ptr;

	struct Timestamp { Genode::uint64_t ms_since_1970; };

	enum class Node_type {
		DIRECTORY,
		SYMLINK,
		CONTINUOUS_FILE,
		TRANSACTIONAL_FILE
	};

	struct Node_rwx
	{
		bool readable;
		bool writeable;
		bool executable;

		static Node_rwx ro()  { return { .readable   = true,
		                                 .writeable  = false,
		                                 .executable = false }; }

		static Node_rwx wo()  { return { .readable   = false,
		                                 .writeable  = true,
		                                 .executable = false }; }

		static Node_rwx rw()  { return { .readable   = true,
		                                 .writeable  = true,
		                                 .executable = false }; }

		static Node_rwx rx()  { return { .readable   = true,
		                                 .writeable  = false,
		                                 .executable = true }; }

		static Node_rwx rwx() { return { .readable   = true,
		                                 .writeable  = true,
		                                 .executable = true }; }
	};

	using Absolute_path = Genode::Path<MAX_PATH_LEN>;

	struct Scanner_policy_path_element
	{
		static bool identifier_char(char c, unsigned /* i */)
		{
			return (c != '/') && (c != 0);
		}

		static bool end_of_quote(const char *s)
		{
			return s[0] != '\\' && s[1] == '\"';
		}
	};
}

#endif /* _INCLUDE__VFS__TYPES_H_ */
