/*
 * \brief  Test for the Callable utility
 * \author Norman Feske
 * \date   2025-01-14
 */

/*
 * Copyright (C) 2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/log.h>
#include <base/component.h>
#include <base/node.h>
#include <util/callable.h>

using namespace Genode;


struct Action : Interface
{
	/*
	 * A functor argument taking 3 ints and returning one int.
	 */
	using With_3_numbers = Callable<int, int, int, int>;

	virtual int _compute(With_3_numbers::Ft const &) const = 0;

	int compute(auto const &fn) const { return _compute( With_3_numbers::Fn { fn } ); }


	/*
	 * A functor argument taking an Node const &, without return value
	 */
	using With_node = Callable<void, Node const &>;

	virtual void _with_node(With_node::Ft const &) = 0;

	void with_node(auto const &fn) { _with_node( With_node::Fn { fn } ); }
};


static void test(Action &action)
{
	int const result = action.compute([&] (int a, int b, int c) {
		return a + b + c; });

	log("result of action.compute: ", result);

	action.with_node([&] (Node const &node) {
		log("accessing node, state=",
		    node.attribute_value("state", String<16>())); });
}


void Component::construct(Env &)
{
	log("--- callable test ---");

	struct Test_action : Action
	{
		int _compute(With_3_numbers::Ft const &fn) const override
		{
			return fn(10, 11, 13);
		}

		void _with_node(With_node::Ft const &fn) override
		{
			fn(Node { String<50>("<power state=\"reset\"/>") });
		}
	} action { };

	test(action);

	log("--- finished callable test ---");
}
