/*
 * \brief  Basic types used by decorator
 * \author Norman Feske
 * \date   2014-01-09
 */

/*
 * Copyright (C) 2014-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__DECORATOR__TYPES_H_
#define _INCLUDE__DECORATOR__TYPES_H_

/* Genode includes */
#include <gui_session/gui_session.h>
#include <util/color.h>
#include <util/geometry.h>
#include <util/color.h>
#include <util/dirty_rect.h>
#include <util/list_model.h>
#include <util/interface.h>
#include <os/surface.h>
#include <base/node.h>

namespace Decorator {

	using namespace Genode;

	using Point      = Surface_base::Point;
	using Area       = Surface_base::Area;
	using Rect       = Surface_base::Rect;
	using Dirty_rect = Genode::Dirty_rect<Rect, 3>;

	/**
	 * Read color attribute from node
	 */
	static inline Color color(Node const &color)
	{
		return color.attribute_value("color", Color::black());
	}
}

#endif /* _INCLUDE__DECORATOR__TYPES_H_ */
