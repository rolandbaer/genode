/*
 * \brief  Set of present windows
 * \author Norman Feske
 * \date   2018-09-26
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _WINDOW_LIST_H_
#define _WINDOW_LIST_H_

/* local includes */
#include <window.h>
#include <layout_rules.h>

namespace Window_layouter { class Window_list; }


class Window_layouter::Window_list
{
	public:

		struct Action : Interface
		{
			virtual void window_list_changed() = 0;
		};

	private:

		Env                     &_env;
		Allocator               &_alloc;
		Action                  &_action;
		Focus_history           &_focus_history;
		Decorator_margins const &_decorator_margins;

		List_model<Window> _list { };

		Attached_rom_dataspace _rom { _env, "window_list" };

		Signal_handler<Window_list> _rom_handler {
			_env.ep(), *this, &Window_list::_handle_rom };

		void _handle_rom()
		{
			_rom.update();

			/* import window-list changes */
			_list.update_from_node(_rom.node(),

				[&] (Node const &node) -> Window &
				{
					Window_id const id { node.attribute_value("id", 0U) };

					Area const initial_size = Area::from_node(node);

					Window::Label const label =
						node.attribute_value("label", Window::Label());

					return *new (_alloc)
						Window(id, label, initial_size,
						       _focus_history, _decorator_margins);
				},

				[&] (Window &window) { destroy(_alloc, &window); },

				[&] (Window &w, Node const &node)
				{
					w.client_size(Area::from_node(node));
					w.title      (node.attribute_value("title", Window::Title("")));
					w.has_alpha  (node.attribute_value("has_alpha",  false));
					w.hidden     (node.attribute_value("hidden",     false));
					w.resizeable (node.attribute_value("resizeable", false));
				}
			);

			/* notify main program */
			_action.window_list_changed();
		}

	public:

		Window_list(Env                     &env,
		            Allocator               &alloc,
		            Action                  &action,
		            Focus_history           &focus_history,
		            Decorator_margins const &decorator_margins)
		:
			_env(env),
			_alloc(alloc),
			_action(action),
			_focus_history(focus_history),
			_decorator_margins(decorator_margins)
		{
			_rom.sigh(_rom_handler);
		}

		void initial_import() { _handle_rom(); }

		void dissolve_windows_from_assignments()
		{
			_list.for_each([&] (Window &win) {
				win.dissolve_from_assignment(); });
		}

		void with_window(Window_id id, auto const &fn)
		{
			_list.for_each([&] (Window &win) {
				if (win.id == id)
					fn(win); });
		}

		void with_window(Window_id id, auto const &fn) const
		{
			_list.for_each([&] (Window const &win) {
				if (win.id == id)
					fn(win); });
		}

		void for_each_window(auto const &fn)       { _list.for_each(fn); }
		void for_each_window(auto const &fn) const { _list.for_each(fn); }
};

#endif /* _WINDOW_LIST_H_ */
