/*
 * \brief  File system stress tester
 * \author Emery Hemingway
 * \date   2015-08-29
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/*
 * This test populates the VFS as follows:
 *
 * A directory is created at root for each thread with
 * sequential names. For each of these directories, a
 * subtree is generated until the maximum path depth
 * is reached at each branch. The subtree is layed out
 * like this:
 *
 * a
 * |\
 * a b
 * |\ \
 * a b b
 * |\ \ \
 * a b b b
 * |\ \ \ \
 * . . . . .
 *
 */

/* Genode includes */
#include <vfs/simple_env.h>
#include <timer_session/connection.h>
#include <base/heap.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/exception.h>

using namespace Genode;

inline void assert_open(Vfs::Directory_service::Open_result r)
{
	using Result = Vfs::Directory_service::Open_result;
	switch (r) {
	case Result::OPEN_OK: return;
	case Result::OPEN_ERR_NAME_TOO_LONG:
		error("OPEN_ERR_NAME_TOO_LONG"); break;
	case Result::OPEN_ERR_UNACCESSIBLE:
		error("OPEN_ERR_UNACCESSIBLE"); break;
	case Result::OPEN_ERR_NO_SPACE:
		error("OPEN_ERR_NO_SPACE"); break;
	case Result::OPEN_ERR_NO_PERM:
		error("OPEN_ERR_NO_PERM"); break;
	case Result::OPEN_ERR_EXISTS:
		error("OPEN_ERR_EXISTS"); break;
	case Result::OPEN_ERR_OUT_OF_RAM:
		error("OPEN_ERR_OUT_OF_RAM"); break;
	case Result::OPEN_ERR_OUT_OF_CAPS:
		error("OPEN_ERR_OUT_OF_CAPS"); break;
	}
	throw Exception();
}

inline void assert_opendir(Vfs::Directory_service::Opendir_result r)
{
	using Result = Vfs::Directory_service::Opendir_result;
	switch (r) {
	case Result::OPENDIR_OK: return;
	case Result::OPENDIR_ERR_LOOKUP_FAILED:
		error("OPENDIR_ERR_LOOKUP_FAILED"); break;
	case Result::OPENDIR_ERR_NAME_TOO_LONG:
		error("OPENDIR_ERR_NAME_TOO_LONG"); break;
	case Result::OPENDIR_ERR_NODE_ALREADY_EXISTS:
		error("OPENDIR_ERR_NODE_ALREADY_EXISTS"); break;
	case Result::OPENDIR_ERR_NO_SPACE:
		error("OPENDIR_ERR_NO_SPACE"); break;
	case Result::OPENDIR_ERR_OUT_OF_RAM:
		error("OPENDIR_ERR_OUT_OF_RAM"); break;
	case Result::OPENDIR_ERR_OUT_OF_CAPS:
		error("OPENDIR_ERR_OUT_OF_CAPS"); break;
	case Result::OPENDIR_ERR_PERMISSION_DENIED:
		error("OPENDIR_ERR_PERMISSION_DENIED"); break;
	}
	throw Exception();
}

inline void assert_write(Vfs::File_io_service::Write_result r)
{
	using Result = Vfs::File_io_service::Write_result;
	switch (r) {
	case Result::WRITE_OK: return;
	case Result::WRITE_ERR_WOULD_BLOCK:
		error("WRITE_ERR_WOULD_BLOCK"); break;
	case Result::WRITE_ERR_INVALID:
		error("WRITE_ERR_INVALID"); break;
	case Result::WRITE_ERR_IO:
		error("WRITE_ERR_IO"); break;
	}
	throw Exception();
}

inline void assert_read(Vfs::File_io_service::Read_result r)
{
	using Result = Vfs::File_io_service::Read_result;
	switch (r) {
	case Result::READ_OK: return;
	case Result::READ_QUEUED:
		error("READ_QUEUED"); break;
	case Result::READ_ERR_WOULD_BLOCK:
		error("READ_ERR_WOULD_BLOCK"); break;
	case Result::READ_ERR_INVALID:
		error("READ_ERR_INVALID"); break;
	case Result::READ_ERR_IO:
		error("READ_ERR_IO"); break;
	}
	throw Exception();
}

inline void assert_unlink(Vfs::Directory_service::Unlink_result r)
{
	using Result = Vfs::Directory_service::Unlink_result;
	switch (r) {
	case Result::UNLINK_OK: return;
	case Result::UNLINK_ERR_NO_ENTRY:
		error("UNLINK_ERR_NO_ENTRY"); break;
	case Result::UNLINK_ERR_NO_PERM:
		error("UNLINK_ERR_NO_PERM"); break;
	case Result::UNLINK_ERR_NOT_EMPTY:
		error("UNLINK_ERR_NOT_EMPTY"); break;
	}
	throw Exception();
}

static int MAX_DEPTH;

using Path = Genode::Path<Vfs::MAX_PATH_LEN>;


struct Stress_test
{
	::Path             path;
	Vfs::file_size     count;
	Vfs::File_system  &vfs;
	Genode::Allocator &alloc;

	Stress_test(Vfs::File_system &vfs, Genode::Allocator &alloc, char const *parent)
	: path(parent), count(0), vfs(vfs), alloc(alloc) { }
};


struct Mkdir_test : public Stress_test
{
	void mkdir_b(int depth)
	{
		if (++depth > MAX_DEPTH) return;

		path.append("/b");
		Vfs::Vfs_handle *dir_handle;
		assert_opendir(vfs.opendir(path.base(), true, &dir_handle, alloc));
		dir_handle->close();
		++count;
		mkdir_b(depth);
	}

	void mkdir_a(int depth)
	{
		if (++depth > MAX_DEPTH) return;

		size_t path_len = strlen(path.base());

		Vfs::Vfs_handle *dir_handle;

		path.append("/b");
		assert_opendir(vfs.opendir(path.base(), true, &dir_handle, alloc));
		dir_handle->close();
		++count;
		mkdir_b(depth);

		path.base()[path_len] = '\0';

		path.append("/a");
		assert_opendir(vfs.opendir(path.base(), true, &dir_handle, alloc));
		dir_handle->close();
		++count;
		mkdir_a(depth);
	}

	Mkdir_test(Vfs::File_system &vfs, Genode::Allocator &alloc, char const *parent)
	: Stress_test(vfs, alloc, parent)
	{
		try { mkdir_a(1); } catch (...) {
			error("failed at '", path, "' after ", count, " directories");
			throw;
		}
	}

	Vfs::file_size wait()
	{
		return count;
	}
};


struct Populate_test : public Stress_test
{
	void populate(int depth)
	{
		if (++depth > MAX_DEPTH) return;

		using namespace Vfs;

		size_t path_len = 1+strlen(path.base());
		char dir_type = *(path.base()+(path_len-2));

		path.append("/c");
		{
			Vfs_handle *handle = nullptr;
			assert_open(vfs.open(
				path.base(), Directory_service::OPEN_MODE_CREATE, &handle, alloc));
			Vfs_handle::Guard guard(handle);
			++count;
		}

		switch (dir_type) {
		case 'a':
			path.base()[path_len] = '\0';
			path.append("a");
			populate(depth);
			[[fallthrough]];

		case 'b':
			path.base()[path_len] = '\0';
			path.append("b");
			populate(depth);
			return;

		default:
			error("bad directory '", Char(dir_type), "' at the end of '", path, "'");
			throw Exception();
		}
	}

	Populate_test(Vfs::File_system &vfs, Genode::Allocator &alloc, char const *parent)
	: Stress_test(vfs, alloc, parent)
	{
		::Path start_path(path.base());
		try {
			path.append("/a");
			populate(1);

			path.import(start_path.base());
			path.append("/b");
			populate(1);
		} catch (...) {
			error("failed at '", path, "' after ", count, " files");
			throw;
		}
	}

	Vfs::file_size wait()
	{
		return count;
	}
};


struct Write_test : public Stress_test
{
	Vfs::Env::Io &_io;

	void write(int depth)
	{
		if (++depth > MAX_DEPTH) return;

		size_t path_len = 1+strlen(path.base());
		char dir_type = *(path.base()+(path_len-2));

		using namespace Vfs;

		path.append("/c");
		{
			Vfs_handle *handle = nullptr;
			assert_open(vfs.open(
				path.base(), Directory_service::OPEN_MODE_WRONLY, &handle, alloc));
			Vfs_handle::Guard guard(handle);

			size_t n;
			assert_write(handle->fs().write(
				handle, Const_byte_range_ptr(path.base(), path_len), n));
			handle->fs().queue_sync(handle);
			while (handle->fs().complete_sync(handle) ==
			       Vfs::File_io_service::SYNC_QUEUED)
				_io.commit_and_wait();
			count += n;
		}

		switch (dir_type) {
		case 'a':
			path.base()[path_len] = '\0';
			path.append("a");
			write(depth);
			[[fallthrough]];

		case 'b':
			path.base()[path_len] = '\0';
			path.append("b");
			write(depth);
			return;

		default:
			error("bad directory ", Char(dir_type), " at the end of '", path, "'");
			throw Exception();
		}
	}

	Write_test(Vfs::File_system &vfs, Genode::Allocator &alloc,
	           char const *parent, Vfs::Env::Io &io)
	:
		Stress_test(vfs, alloc, parent), _io(io)
	{
		size_t path_len = strlen(path.base());
		try {
			path.append("/a");
			write(1);

			path.base()[path_len] = '\0';
			path.append("/b");
			write(1);
		} catch (...) {
			error("failed at ",path," after writing ",count," bytes");
			throw;
		}
	}

	Vfs::file_size wait()
	{
		return count;
	}
};


struct Read_test : public Stress_test
{
	Vfs::Env::Io &_io;

	void read(int depth)
	{
		if (++depth > MAX_DEPTH) return;

		size_t path_len = 1+strlen(path.base());
		char dir_type = *(path.base()+(path_len-2));

		using namespace Vfs;

		path.append("/c");
		{
			Vfs_handle *handle = nullptr;
			assert_open(vfs.open(
				path.base(), Directory_service::OPEN_MODE_RDONLY, &handle, alloc));
			Vfs_handle::Guard guard(handle);

			char tmp[MAX_PATH_LEN];
			size_t n;
			handle->fs().queue_read(handle, sizeof(tmp));

			Vfs::File_io_service::Read_result read_result;

			Byte_range_ptr const dst { tmp, sizeof(tmp) };

			while ((read_result =
			        handle->fs().complete_read(handle, dst, n)) ==
			       Vfs::File_io_service::READ_QUEUED)
				_io.commit_and_wait();

			assert_read(read_result);

			if (strcmp(path.base(), tmp, (size_t)n))
				error("read returned bad data");
			count += n;
		}

		switch (dir_type) {
		case 'a':
			path.base()[path_len] = '\0';
			path.append("a");
			read(depth);
			[[fallthrough]];

		case 'b':
			path.base()[path_len] = '\0';
			path.append("/b");
			read(depth);
			return;

		default:
			error("bad directory ", Char(dir_type), " at the end of '", path, "'");
			throw Exception();
		}
	}

	Read_test(Vfs::File_system &vfs, Genode::Allocator &alloc, char const *parent,
	          Vfs::Env::Io &io)
	:
		Stress_test(vfs, alloc, parent), _io(io)
	{
		size_t path_len = strlen(path.base());
		try {
			path.append("/a");
			read(1);

			path.base()[path_len] = '\0';
			path.append("/b");
			read(1);
		} catch (...) {
			error("failed at ",path," after reading ",count," bytes");
			throw;
		}
	}

	Vfs::file_size wait()
	{
		return count;
	}
};


struct Unlink_test : public Stress_test
{
	Vfs::Env::Io &_io;

	void empty_dir(char const *path)
	{
		::Path subpath(path);
		subpath.append("/");

		Vfs::Vfs_handle *dir_handle;
		assert_opendir(vfs.opendir(path, false, &dir_handle, alloc));

		Vfs::Directory_service::Dirent dirent { };
		for (Vfs::file_size i = vfs.num_dirent(path); i;) {
			dir_handle->seek(--i * sizeof(dirent));
			dir_handle->fs().queue_read(dir_handle, sizeof(dirent));

			Byte_range_ptr const dst { (char*)&dirent, sizeof(dirent) };
			size_t out_count;

			while (dir_handle->fs().complete_read(dir_handle, dst, out_count) ==
			       Vfs::File_io_service::READ_QUEUED)
				_io.commit_and_wait();

			subpath.append(dirent.name.buf);
			switch (dirent.type) {
			case Vfs::Directory_service::Dirent_type::END:
				error("reached the end prematurely");
				throw Exception();

			case Vfs::Directory_service::Dirent_type::DIRECTORY:
				empty_dir(subpath.base());
				[[fallthrough]];

			default:
				try {
					assert_unlink(vfs.unlink(subpath.base()));
					++count;
				} catch (...) {
					error("unlink ", subpath," failed");
					throw;
				}
				subpath.strip_last_element();
			}
		}

		dir_handle->close();
	}

	Unlink_test(Vfs::File_system &vfs, Genode::Allocator &alloc,
	            char const *parent, Vfs::Env::Io &io)
	:
		Stress_test(vfs, alloc, parent), _io(io)
	{
		using Result = Vfs::Directory_service::Unlink_result;
		try {
			Result r = vfs.unlink(path.base());
			switch (r) {
			case Result::UNLINK_ERR_NOT_EMPTY:
				log("recursive unlink not supported");
				empty_dir(path.base());
				r = vfs.unlink(path.base());
				[[fallthrough]];

			case Result::UNLINK_OK:
				log("recursive unlink supported");
				++count;
				return;

			default: break;
			}
			assert_unlink(r);
		} catch (...) {
			error("unlink ",path," failed");
			throw;
		}
	}

	Vfs::file_size wait()
	{
		return count;
	}
};

void die(Genode::Env &env, int code) { env.parent().exit(code); }

void Component::construct(Genode::Env &env)
{
	enum { ROOT_TREE_COUNT = 6 };

	Genode::Heap heap(env.ram(), env.rm());

	Attached_rom_dataspace config_rom(env, "config");

	Vfs::Simple_env vfs_env = config_rom.node().with_sub_node("vfs",
		[&] (Node const &config) -> Vfs::Simple_env {
			return { env, heap, config }; },
		[&] () -> Vfs::Simple_env {
			error("VFS not configured");
			return { env, heap, Node() }; });

	Vfs::File_system &vfs_root = vfs_env.root_dir();

	Vfs::Vfs_handle *vfs_root_handle;
	vfs_root.opendir("/", false, &vfs_root_handle, heap);

	auto vfs_root_sync = [&] ()
	{
		while (!vfs_root_handle->fs().queue_sync(vfs_root_handle))
			vfs_env.io().commit_and_wait();

		while (vfs_root_handle->fs().complete_sync(vfs_root_handle) ==
		       Vfs::File_io_service::SYNC_QUEUED)
			vfs_env.io().commit_and_wait();
	};

	String<Vfs::MAX_PATH_LEN> path { };

	MAX_DEPTH = config_rom.node().attribute_value("depth", 16U);

	uint64_t elapsed_ms;
	Timer::Connection timer(env);

	/* populate the directory file system at / */
	vfs_root.num_dirent("/");

	size_t initial_consumption = env.pd().used_ram().value;

	/**************************
	 ** Generate directories **
	 **************************/
	{
		Vfs::file_size count = 0;
		log("generating directory surface...");
		elapsed_ms = timer.elapsed_ms();

		for (int i = 0; i < ROOT_TREE_COUNT; ++i) {
			path = { "/", i };
			Vfs::Vfs_handle *dir_handle;
			vfs_root.opendir(path.string(), true, &dir_handle, heap);
			dir_handle->close();
			Mkdir_test test(vfs_root, heap, path.string());
			count += test.wait();
		}
		elapsed_ms = timer.elapsed_ms() - elapsed_ms;

		vfs_root_sync();

		if (count > 0)
			log("created ",count," empty directories, ",
			    (elapsed_ms*1000)/count,"μs/op , ",
			    env.pd().used_ram().value/1024,"KiB consumed");
	}


	/********************
	 ** Generate files **
	 ********************/
	{
		Vfs::file_size count = 0;
		log("generating files...");
		elapsed_ms = timer.elapsed_ms();

		for (int i = 0; i < ROOT_TREE_COUNT; ++i) {
			path = { "/", i };
			Populate_test test(vfs_root, heap, path.string());
			count += test.wait();
		}

		elapsed_ms = timer.elapsed_ms() - elapsed_ms;

		vfs_root_sync();

		if (count > 0)
			log("created ",count," empty files, ",
			    (elapsed_ms*1000)/count,"μs/op, ",
			    env.pd().used_ram().value/1024,"KiB consumed");
	}


	/*****************
	 ** Write files **
	 *****************/

	if (!config_rom.node().attribute_value("write", true)) {
		elapsed_ms = timer.elapsed_ms();
		log("total: ",elapsed_ms,"ms, ",env.pd().used_ram().value/1024,"K consumed");
		return die(env, 0);
	}
	{
		Vfs::file_size count = 0;
		log("writing files...");
		elapsed_ms = timer.elapsed_ms();

		for (int i = 0; i < ROOT_TREE_COUNT; ++i) {
			path = { "/", i };
			Write_test test(vfs_root, heap, path.string(), vfs_env.io());
			count += test.wait();

		}

		elapsed_ms = timer.elapsed_ms() - elapsed_ms;

		vfs_root_sync();

		if (elapsed_ms > 0)
			log("wrote ",count," bytes, ",
			    count/elapsed_ms,"kB/s, ",
			    env.pd().used_ram().value/1024,"KiB consumed");
		else
			log("wrote ",count," bytes, ",
			    env.pd().used_ram().value/1024,"KiB consumed");
	}


	/*****************
	 ** Read files **
	 *****************/

	if (!config_rom.node().attribute_value("read", true)) {
		elapsed_ms = timer.elapsed_ms();

		log("total: ",elapsed_ms,"ms, ",env.pd().used_ram().value/1024,"KiB consumed");
		return die(env, 0);
	}
	{
		Vfs::file_size count = 0;
		log("reading files...");
		elapsed_ms = timer.elapsed_ms();

		for (int i = 0; i < ROOT_TREE_COUNT; ++i) {
			path = { "/", i };
			Read_test test(vfs_root, heap, path.string(), vfs_env.io());
			count += test.wait();
		}

		elapsed_ms = timer.elapsed_ms() - elapsed_ms;

		vfs_root_sync();

		if (elapsed_ms > 0)
			log("read ",count," bytes, ",
			    count/elapsed_ms,"kB/s, ",
			    env.pd().used_ram().value/1024,"KiB consumed");
		else
			log("read ",count," bytes, ",
			    env.pd().used_ram().value/1024,"KiB consumed");
	}


	/******************
	 ** Unlink files **
	 ******************/

	if (!config_rom.node().attribute_value("unlink", true)) {
		elapsed_ms = timer.elapsed_ms();
		log("total: ",elapsed_ms,"ms, ",env.pd().used_ram().value/1024,"KiB consumed");
		return die(env, 0);

	}
	{
		Vfs::file_size count = 0;

		log("unlink files...");
		elapsed_ms = timer.elapsed_ms();

		for (int i = 0; i < ROOT_TREE_COUNT; ++i) {
			path = { "/", i };
			Unlink_test test(vfs_root, heap, path.string(), vfs_env.io());
			count += test.wait();

		}

		elapsed_ms = timer.elapsed_ms() - elapsed_ms;

		vfs_root_sync();

		log("unlinked ",count," files in ",elapsed_ms,"ms, ",
		    env.pd().used_ram().value/1024,"KiB consumed");
	}

	log("total: ",timer.elapsed_ms(),"ms, ",
	    env.pd().used_ram().value/1024,"KiB consumed");

	size_t outstanding = env.pd().used_ram().value - initial_consumption;
	if (outstanding) {
		if (outstanding < 1024)
			error(outstanding, "B not freed after unlink and sync!");
		else
			error(outstanding/1024,"KiB not freed after unlink and sync!");
	}

	die(env, 0);
}
