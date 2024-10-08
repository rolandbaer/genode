/*
 * \brief  Types used the depot download manager
 * \author Norman Feske
 * \date   2018-01-11
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _TYPES_H_
#define _TYPES_H_

#include <util/string.h>
#include <base/exception.h>
#include <depot/archive.h>

namespace Depot_download_manager {

	using namespace Depot;

	using Rom_name = String<32>;
	using Url      = String<160>;
	using Path     = String<160>;

	struct Depot_query_version { unsigned value; };
	struct Fetchurl_version    { unsigned value; };

	struct Require_verify;

	struct Pubkey_known { bool value; };
}


/**
 * Argument type for propagating 'verify' install attributes to imports
 */
struct Depot_download_manager::Require_verify
{
	bool value;

	static Require_verify from_xml(Xml_node const &node)
	{
		return Require_verify { node.attribute_value("require_verify", true) };
	}

	void gen_attr(Xml_generator &xml) const
	{
		if (!value)
			xml.attribute("require_verify", "no");
	}
};


namespace Genode {

	/**
	 * Strip off the last element from 'path'
	 */
	template <size_t N>
	static String<N> without_last_path_element(String<N> const &path)
	{
		unsigned last_slash = 0;
		char const * const s = path.string();
		for (unsigned i = 0; s[i]; i++)
			if (s[i] == '/')
				last_slash = i;

		return String<N>(Cstring(s, last_slash));
	}
}

#endif /* _TYPES_H_ */

