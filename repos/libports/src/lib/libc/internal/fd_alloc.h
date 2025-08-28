/*
 * \brief  file descriptor allocator interface
 * \author Christian Prochaska 
 * \date   2010-01-21
 */

/*
 * Copyright (C) 2010-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _LIBC_PLUGIN__FD_ALLOC_H_
#define _LIBC_PLUGIN__FD_ALLOC_H_

/* Genode includes */
#include <base/mutex.h>
#include <base/log.h>
#include <base/node.h>
#include <os/path.h>
#include <base/allocator.h>
#include <base/id_space.h>
#include <util/bit_allocator.h>

/* libc includes */
#include <stdlib.h>
#include <string.h>

/* libc-internal includes */
#include <internal/plugin.h>

enum { MAX_NUM_FDS = 1024 };

namespace Libc {

	/**
	 * Plugin-specific file-descriptor context
	 */
	struct Plugin_context { virtual ~Plugin_context() { } };

	enum { ANY_FD = -1 };

	struct File_descriptor;

	class File_descriptor_allocator;
}


struct Libc::File_descriptor
{
	Genode::Mutex mutex { };

	using Id_space = Genode::Id_space<File_descriptor>;
	Id_space::Element _elem;

	int const libc_fd = _elem.id().value;

	char const *fd_path = nullptr;  /* for 'fchdir', 'fstat' */

	Plugin         *plugin;
	Plugin_context *context;

	int  flags    = 0;  /* for 'fcntl' */
	bool cloexec  = 0;  /* for 'fcntl' */
	bool modified = false;

	File_descriptor(Id_space &id_space, Plugin &plugin, Plugin_context &context,
	                Id_space::Id id)
	: _elem(*this, id_space, id), plugin(&plugin), context(&context) { }

	void path(char const *newpath);
};


class Libc::File_descriptor_allocator
{
	private:

		Genode::Mutex _mutex;

		Genode::Allocator &_alloc;

		using Id_space = File_descriptor::Id_space;

		Id_space _id_space;

		using Id_bit_alloc = Genode::Bit_allocator<MAX_NUM_FDS>;

		Id_bit_alloc _id_allocator;

	public:

		/**
		 * Constructor
		 */
		File_descriptor_allocator(Genode::Allocator &_alloc);

		/**
		 * Allocate file descriptor
		 */
		File_descriptor *alloc(Plugin *plugin, Plugin_context *context, int libc_fd = -1);

		/**
		 * Release file descriptor
		 */
		void free(File_descriptor *fdo);

		/**
		 * Prevent the use of the specified file descriptor
		 */
		void preserve(int libc_fd);

		File_descriptor *find_by_libc_fd(int libc_fd);

		/**
		 * Return any file descriptor with close-on-execve flag set
		 *
		 * \return pointer to file descriptor, or
		 *         nullptr is no such file descriptor exists
		 */
		File_descriptor *any_cloexec_libc_fd();

		/**
		 * Update seek state of file descriptor with append flag set.
		 */
		void update_append_libc_fds();

		/**
		 * Return file-descriptor ID of any open file, or -1 if no file is
		 * open
		 */
		int any_open_fd();

		void generate_info(Genode::Generator &);
};

#endif /* _LIBC_PLUGIN__FD_ALLOC_H_ */
