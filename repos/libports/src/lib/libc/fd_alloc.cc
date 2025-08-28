/*
 * \brief  File-descriptor allocator implementation
 * \author Christian Prochaska
 * \author Norman Feske
 * \date   2010-01-21
 */

/*
 * Copyright (C) 2010-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <util/construct_at.h>
#include <base/env.h>
#include <base/log.h>

/* libc includes */
#include <fcntl.h>
#include <unistd.h>

/* libc-internal includes */
#include <internal/fd_alloc.h>
#include <internal/init.h>

using namespace Libc;


static Allocator *_alloc_ptr;


void Libc::init_fd_alloc(Allocator &alloc) { _alloc_ptr = &alloc; }


File_descriptor_allocator::File_descriptor_allocator(Allocator &alloc)
: _alloc(alloc)
{ }


File_descriptor *File_descriptor_allocator::alloc(Plugin *plugin,
                                                  Plugin_context *context,
                                                  int libc_fd)
{
	Mutex::Guard guard(_mutex);

	bool const any_fd = (libc_fd < 0);
	Id_space::Id id {(unsigned)libc_fd};

	if (any_fd) {
		auto const allocated_bit = _id_allocator.alloc();
		if (allocated_bit.failed())
			return nullptr;

		allocated_bit.with_result([&] (addr_t n) { id.value = n; },
		                          [&] (Id_bit_alloc::Error) { /* handled above */ });
	} else {
		if (_id_allocator.alloc_addr(addr_t(libc_fd)).failed())
			return nullptr;
	}

	return new (_alloc) File_descriptor(_id_space, *plugin, *context, id);
}


void File_descriptor_allocator::free(File_descriptor *fdo)
{
	Mutex::Guard guard(_mutex);

	if (fdo->fd_path)
		_alloc.free((void *)fdo->fd_path, ::strlen(fdo->fd_path) + 1);

	_id_allocator.free(fdo->libc_fd);
	destroy(_alloc, fdo);
}


void File_descriptor_allocator::preserve(int fd)
{
	if (!find_by_libc_fd(fd))
		alloc(nullptr, nullptr, fd);
}


File_descriptor *File_descriptor_allocator::find_by_libc_fd(int libc_fd)
{
	Mutex::Guard guard(_mutex);

	if (libc_fd < 0)
		return nullptr;

	File_descriptor *result = nullptr;

	try {
		Id_space::Id const id {(unsigned)libc_fd};
		_id_space.apply<File_descriptor>(id, [&] (File_descriptor &fd) {
			result = &fd; });
	} catch (Id_space::Unknown_id) { }

	return result;
}


File_descriptor *File_descriptor_allocator::any_cloexec_libc_fd()
{
	Mutex::Guard guard(_mutex);

	File_descriptor *result = nullptr;

	_id_space.for_each<File_descriptor>([&] (File_descriptor &fd) {
		if (!result && fd.cloexec)
			result = &fd; });

	return result;
}


void File_descriptor_allocator::update_append_libc_fds()
{
	Mutex::Guard guard(_mutex);

	_id_space.for_each<File_descriptor>([&] (File_descriptor &fd) {
		if (fd.flags & O_APPEND)
			fd.plugin->lseek(&fd, 0, SEEK_END);
	});
}


int File_descriptor_allocator::any_open_fd()
{
	Mutex::Guard guard(_mutex);

	int result = -1;
	_id_space.apply_any<File_descriptor>([&] (File_descriptor &fd) {
	 	result = fd.libc_fd; });

	return result;
}


void File_descriptor_allocator::generate_info(Generator &g)
{
	Mutex::Guard guard(_mutex);

	_id_space.for_each<File_descriptor>([&] (File_descriptor &fd) {
		g.node("fd", [&] () {

			g.attribute("id", fd.libc_fd);

			if (fd.fd_path)
				g.attribute("path", fd.fd_path);

			if (fd.cloexec)
				g.attribute("cloexec", "yes");

			if (((fd.flags & O_ACCMODE) != O_WRONLY))
				g.attribute("readable", "yes");

			if (((fd.flags & O_ACCMODE) != O_RDONLY))
				g.attribute("writeable", "yes");

			if (fd.plugin) {
				::off_t const seek = fd.plugin->lseek(&fd, 0, SEEK_CUR);
				if (seek)
					g.attribute("seek", seek);
			}
		});
	});
}


void File_descriptor::path(char const *newpath)
{
	if (fd_path)
		warning("may leak former FD path memory");

	if (!_alloc_ptr) {
		error("missing call of 'init_fd_alloc'");
		return;
	}

	if (newpath) {
		size_t const path_size = ::strlen(newpath) + 1;
		char *buf = (char*)_alloc_ptr->alloc(path_size);
		if (!buf) {
			error("could not allocate path buffer for libc_fd ", libc_fd);
			return;
		}
		::memcpy(buf, newpath, path_size);
		fd_path = buf;
	} else
		fd_path = 0;
}


/********************
 ** Libc functions **
 ********************/

extern "C" int __attribute__((weak)) getdtablesize(void) { return MAX_NUM_FDS; }
