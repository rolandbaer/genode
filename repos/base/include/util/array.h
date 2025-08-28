/*
 * \brief   Array class
 * \author  Stefan Kalkowski
 * \date    2016-09-30
 */

/*
 * Copyright (C) 2016-2021 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__UTIL__ARRAY_H_
#define _INCLUDE__UTIL__ARRAY_H_

#include <base/error.h>

namespace Genode { template <typename, unsigned> class Array; }


/**
 * Array with static size
 *
 * \param T    array element type
 * \param MAX  maximum number of array elements
 */
template <typename T, unsigned MAX>
class Genode::Array
{
	protected:

		unsigned _count { 0 };
		T        _objs[MAX];

	public:

		/**
		 * Constructs an empty array
		 */
		Array() {}

		/**
		 * Constructs a filled array
		 *
		 * \param args  variable count of array elements
		 */
		template<typename ... ARGS>
		Array(ARGS ... args)
		{
			static_assert(sizeof...(ARGS) <= MAX, "Array too small!");
			add(args...);
		}

		/**
		 * Return the count of elements inside the array
		 */
		unsigned count() const { return _count; }

		/**
		 * Return the array element specified by index
		 *
		 * \param idx  the index of the array element
		 *
		 * \throw Index_out_of_bounds
		 */
		T & value(unsigned idx)
		{
			if (idx >= _count)
				raise(Unexpected_error::INDEX_OUT_OF_BOUNDS);
			return _objs[idx];
		}

		/**
		 * Adds a single element to the array.
		 *
		 * The element gets inserted at position 'count()',
		 * and 'count()' is incremented.
		 *
		 * \param obj  the element to be added
		 *
		 * \throw Index_out_of_bounds
		 */
		void add(T obj)
		{
			if ((_count + 1) > MAX)
				raise(Unexpected_error::INDEX_OUT_OF_BOUNDS);
			_objs[_count++] = obj;
		}

		/**
		 * Adds a variable count of elements to the array.
		 *
		 * The elements are getting inserted at position 'count()',
		 * and 'count()' is increased by the number of elements given.
		 *
		 * \param obj   the first element to be added
		 * \param tail  the tail of arguments
		 *
		 * \throw Index_out_of_bounds
		 */
		void add(T obj, auto &&... tail)
		{
			add(obj);
			add(tail...);
		}

		void for_each(auto const &fn)
		{
			for (unsigned idx = 0; idx < _count; idx++)
				fn(idx, _objs[idx]);
		}

		void for_each(auto const &fn) const
		{
			for (unsigned idx = 0; idx < _count; idx++) {
				T const &obj = _objs[idx];
				fn(idx, obj);
			}
		}
};

#endif /* _INCLUDE__UTIL__ARRAY_H_ */
