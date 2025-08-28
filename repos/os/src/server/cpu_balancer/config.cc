/*
 * \brief  Config evaluation
 * \author Alexander Boettcher
 * \date   2020-07-20
 */

/*
 * Copyright (C) 2020 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <base/session_label.h>
#include <base/thread.h>

#include "config.h"

void Cpu::Config::apply(Node const &start, Child_list &sessions)
{
	using Label = String<Session_label::capacity()>;

	start.for_each_sub_node("component", [&](Node const &node) {
		if (!node.has_attribute("label"))
			return;

		Label const label = node.attribute_value("label", Label(""));

		sessions.for_each([&](auto &session) {
			if (!session.match(label))
				return;

			if (node.has_attribute("default_policy")) {
				Cpu::Policy::Name const policy = node.attribute_value("default_policy", Cpu::Policy::Name());
				session.default_policy(policy);
			}

			node.for_each_sub_node("thread", [&](Node const &thread) {
				if (!thread.has_attribute("name") || !thread.has_attribute("policy"))
					return;

				Thread::Name const name = thread.attribute_value("name", Thread::Name());
				Cpu::Policy::Name const policy = thread.attribute_value("policy", Cpu::Policy::Name());

				/* explicitly create invalid width/height */
				/* used during thread construction in policy static case */
				Affinity::Location location { 0, 0, 0, 0};

				if (thread.has_attribute("xpos") && thread.has_attribute("ypos"))
					location = Affinity::Location(thread.attribute_value("xpos", 0U),
					                              thread.attribute_value("ypos", 0U),
					                              1, 1);

				session.update_if_active(name, policy, location);
			});
		});
	});
}

void Cpu::Config::apply_for_thread(Node const &start, Cpu::Session &session,
                                   Thread::Name const &target_thread)
{
	using Label = String<Session_label::capacity()>;

	start.for_each_sub_node("component", [&](Node const &node) {
		if (!node.has_attribute("label"))
			return;

		Label const label = node.attribute_value("label", Label(""));

		if (!session.match(label))
			return;

		node.for_each_sub_node("thread", [&](Node const &thread) {
			if (!thread.has_attribute("name") || !thread.has_attribute("policy"))
				return;

			Thread::Name const name = thread.attribute_value("name", Thread::Name());
			Cpu::Policy::Name const policy = thread.attribute_value("policy", Cpu::Policy::Name());

			if (target_thread != name)
				return;

			/* explicitly create invalid width/height */
			/* used during thread construction in policy static case */
			Affinity::Location location { 0, 0, 0, 0};

			if (thread.has_attribute("xpos") && thread.has_attribute("ypos"))
				location = Affinity::Location(thread.attribute_value("xpos", 0U),
				                              thread.attribute_value("ypos", 0U),
				                              1, 1);

			session.update(name, policy, location);
		});
	});
}
