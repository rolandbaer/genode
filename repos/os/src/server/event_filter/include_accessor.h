/*
 * \brief  Interface for accessing a configuration snippets
 * \author Norman Feske
 * \date   2017-02-10
 */

/*
 * Copyright (C) 2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _EVENT_FILTER__INCLUDE_ACCESSOR_H_
#define _EVENT_FILTER__INCLUDE_ACCESSOR_H_

/* local includes */
#include <types.h>

namespace Event_filter { struct Include_accessor; }


class Event_filter::Include_accessor : Interface
{
	public:

		using Name = String<64>;
		using Type = String<32>;

		struct Include_unavailable : Exception { };

	protected:

		struct Functor : Interface { virtual void apply(Node const &node) const = 0; };

		/*
		 * \throw Include_unavailable
		 */
		virtual void _apply_include(Name const &name, Type const &type, Functor const &) = 0;

	public:

		/**
		 * Call functor 'fn' with the 'Node const &' of the named include
		 *
		 * \throw Include_unavailable
		 */
		template <typename FN>
		void apply_include(Name const &name, Type const &type, FN const &fn)
		{
			struct Functor : Include_accessor::Functor
			{
				FN const &fn;

				Functor(FN const &fn) : fn(fn) { }

				void apply(Node const &node) const override { fn(node); }
			} _functor(fn);

			_apply_include(name, type, _functor);
		}
};

#endif /* _EVENT_FILTER__INCLUDE_ACCESSOR_H_ */
