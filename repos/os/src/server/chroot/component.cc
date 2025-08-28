/*
 * \brief  Change session root server
 * \author Emery Hemingway
 * \date   2016-03-10
 */

/*
 * Copyright (C) 2016 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <file_system/util.h>
#include <file_system_session/connection.h>
#include <os/path.h>
#include <os/session_policy.h>
#include <base/component.h>
#include <base/attached_rom_dataspace.h>
#include <parent/parent.h>
#include <base/service.h>
#include <base/allocator_avl.h>
#include <base/heap.h>

namespace Chroot {
	using namespace Genode;
	struct Main;
}


struct Chroot::Main
{
	enum { PATH_MAX_LEN = 128 };
	using Path = Genode::Path<PATH_MAX_LEN>;

	/**
	 * Object to bind ids between parent and client space.
	 */
	struct Session : Parent::Server
	{
		Parent::Client parent_client { };

		Id_space<Parent::Client>::Element client_id;
		Id_space<Parent::Server>::Element server_id;

		Session(Id_space<Parent::Client> &client_space,
		        Id_space<Parent::Server> &server_space,
		        Parent::Server::Id server_id)
		:
			client_id(parent_client, client_space),
			server_id(*this, server_space, server_id) { }
	};

	Genode::Env &env;

	Id_space<Parent::Server> server_id_space { };

	Heap heap { env.ram(), env.rm() };

	Allocator_avl fs_tx_block_alloc { &heap };

	/**
	 * File-system session for creating new chroot directories
	 */
	File_system::Connection fs {
		env, fs_tx_block_alloc, "/", true, 1 };

	Attached_rom_dataspace session_requests { env, "session_requests" };

	Attached_rom_dataspace config_rom { env, "config" };

	void handle_config_update() { config_rom.update(); }

	void handle_session_request(Node const &request);

	void handle_session_requests()
	{
		session_requests.update();

		Node const &requests = session_requests.node();

		requests.for_each_sub_node([&] (Node const &request) {
			handle_session_request(request);
		});
	}

	Signal_handler<Main> config_update_handler {
		env.ep(), *this, &Main::handle_config_update };

	Signal_handler<Main> session_request_handler {
		env.ep(), *this, &Main::handle_session_requests };

	/**
	 * Constructor
	 */
	Main(Genode::Env &env) : env(env)
	{
		config_rom.sigh(config_update_handler);
		session_requests.sigh(session_request_handler);

		/* handle requests that have queued before or during construction */
		handle_session_requests();
	}

	Session_capability request_session(Parent::Client::Id  const &id,
	                                   Session_state::Args const &args,
	                                   Affinity            const  affinity,
	                                   Node                const &policy)
	{
		using Prefix = String<PATH_MAX_LEN>;

		Path root_path;

		Session_label const label = label_from_args(args.string());

		if (policy.has_attribute("path_prefix")) {
			/* Use a chroot path from policy and label sub-directories */
			Prefix const prefix = policy.attribute_value("path_prefix", Prefix());
			root_path.import(prefix.string());
			root_path.append(path_from_label<Path>(label.prefix().string()).string());
		} else if (policy.has_attribute("path")) {
			/* Use a chroot path from policy */
			root_path.import(policy.attribute_value("path", Prefix()).string());
		} else {
			/* generate implicit chroot path from the label */
			root_path = path_from_label<Path>(label.string());
		}

		/* append client-provided path */
		{
			Session_label const client_root_path = label.last_element();
			bool root_path_valid = true;
			if (client_root_path.string()[0] != '/') {
				warning(label, ": last label element should start with /");
				root_path_valid = false;
			}
			if (client_root_path.string()[min(1ul, client_root_path.length()) - 1] != '/') {
				warning(label, ": last label element should end with /");
				root_path_valid = false;
			}
			if (!root_path_valid)
				throw Service_denied();
			root_path.append(client_root_path.string());
			root_path.remove_trailing('/');
		}

		char const *new_root = root_path.base();

		using namespace File_system;

		/* create the new root directory if it is missing */
		try { fs.close(ensure_dir(fs, new_root)); }
		catch (Node_already_exists) { }
		catch (Permission_denied)   {
			Genode::error(new_root,": permission denied"); throw; }
		catch (Name_too_long)       {
			Genode::error(new_root,": new root too long"); throw; }
		catch (No_space)            {
			Genode::error(new_root,": no space");          throw; }
		catch (...)                 {
			Genode::error(new_root,": unknown error");     throw; }

		/* rewrite the root session argument */
		enum { ARGS_MAX_LEN = Parent::Session_args::MAX_SIZE };
		char new_args[ARGS_MAX_LEN];

		copy_cstring(new_args, args.string(), ARGS_MAX_LEN);

		/* sacrifice the label to make space for the root argument */
		Arg_string::remove_arg(new_args, "label");

		/* enforce writeable policy decision */
		{
			enum { WRITEABLE_ARG_MAX_LEN = 4, };
			char tmp[WRITEABLE_ARG_MAX_LEN];
			Arg_string::find_arg(new_args, "writeable").string(tmp, sizeof(tmp), "no");

			/* session argument */
			bool const writeable_arg =
				Arg_string::find_arg(new_args, "writeable").bool_value(false);

			/* label-based session policy */
			bool const writeable_policy =
				policy.attribute_value("writeable", false);

			bool const writeable = writeable_arg && writeable_policy;
			Arg_string::set_arg(new_args, ARGS_MAX_LEN, "writeable", writeable);
		}

		/* replace last label element by new root path */
		Session_label const rewritten_label =
			prefixed_label(label.prefix(), Session_label(root_path.string(), "/"));

		if (!Arg_string::set_arg_string(new_args, ARGS_MAX_LEN, "label", rewritten_label.string())) {
			warning("label \"", rewritten_label, "\" is too long for session arguments");
			throw Service_denied();
		}

		return env.session("File_system", id, new_args, affinity);
	}
};


void Chroot::Main::handle_session_request(Node const &request)
{
	if (!request.has_attribute("id"))
		return;

	Parent::Server::Id const server_id { request.attribute_value("id", 0UL) };

	if (request.has_type("create")) {

		if (!request.has_sub_node("args"))
			return;

		using Args = Session_state::Args;
		Args const args = request.with_sub_node("args",

		[] (Node const &node) { return Args(Node::Quoted_content(node)); },
		[]                    { return Args(); });

		with_matching_policy(label_from_args(args.string()), config_rom.node(),
			[&] (Node const &policy) {
				Session &session = *new (heap)
					Session(env.id_space(), server_id_space, server_id);
				Session_capability cap =
					request_session(session.client_id.id(), args,
					                Affinity::from_node(request), policy);
				env.parent().deliver_session_cap(server_id, cap);
			},
			[&] {
				Genode::error("no policy defined for '", label_from_args(args.string()), "'");
				env.parent().session_response(server_id, Parent::Session_response::DENIED);
			});
	}

	if (request.has_type("upgrade")) {

		server_id_space.apply<Session>(server_id, [&] (Session &session) {

			size_t ram_quota = request.attribute_value("ram_quota", 0UL);
			size_t cap_quota = request.attribute_value("cap_quota", 0UL);

			String<128> args("ram_quota=", ram_quota, ", cap_quota=", cap_quota);

			env.upgrade(session.client_id.id(), args.string());
			env.parent().session_response(server_id, Parent::Session_response::OK);
		});
	}

	if (request.has_type("close")) {
		server_id_space.apply<Session>(server_id, [&] (Session &session) {
			env.close(session.client_id.id());
			destroy(heap, &session);
			env.parent().session_response(server_id, Parent::Session_response::CLOSED);
		});
	}
}


/***************
 ** Component **
 ***************/

Genode::size_t Component::stack_size() {
	return 2*1024*sizeof(Genode::addr_t); }

void Component::construct(Genode::Env &env)
{
	static Chroot::Main inst(env);
	env.parent().announce("File_system");
}
