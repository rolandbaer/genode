/*
 * \brief  VFS audit plugin
 * \author Emery Hemingway
 * \date   2018-03-12
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <vfs/file_system_factory.h>
#include <vfs/types.h>
#include <log_session/connection.h>

namespace Vfs_audit {
	using namespace Vfs;
	class File_system;
}

class Vfs_audit::File_system : public Vfs::File_system
{
	private:

		class Log : public Genode::Output
		{
			private:

				enum { BUF_SIZE = Genode::Log_session::MAX_STRING_LEN };

				Genode::Log_connection _log;

				char _buf[BUF_SIZE];
				unsigned _num_chars = 0;

				void _flush()
				{
					_buf[_num_chars] = '\0';
					_log.write(Genode::Log_session::String(_buf, _num_chars+1));
					_num_chars = 0;
				}

			public:

				Log(Genode::Env &env, char const *label)
				: _log(env, label) { }

				void out_char(char c) override
				{
					_buf[_num_chars++] = c;
					if (_num_chars >= sizeof(_buf)-1)
						_flush();
				}

				template <typename... ARGS>
				void log(ARGS &&... args)
				{
					Output::out_args(*this, args...);
					_flush();
				}

		} _audit_log;

		template <typename... ARGS>
		void _log(ARGS &&... args) { _audit_log.log(args...); }

		Vfs::File_system &_root_dir;

		Absolute_path const _audit_path;

		Absolute_path _expanded_path { }; /* buffer for 'leaf_path' return value */

		/**
		 * Expand a path to lay within the audit path
		 */
		Absolute_path _expand(char const *path)
		{
			return Absolute_path(path+1, _audit_path.string());
		}

		struct Handle final : Vfs_handle
		{
			Handle(Handle const &);
			Handle &operator = (Handle const &);

			Absolute_path const path;
			Vfs_handle *audit = nullptr;

			void sync_state()
			{
				if (audit)
					audit->seek(Vfs_handle::seek());
			}

			Handle(Vfs_audit::File_system &fs, Genode::Allocator &alloc,
			       int flags, char const *path)
			:
				Vfs_handle(fs, fs, alloc, flags), path(path)
			{ };
		};

	public:

		File_system(Vfs::Env &env, Node const &config)
		:
			_audit_log(env.env(), config.attribute_value("label", Genode::String<64>("audit")).string()),
			_root_dir(env.root_dir()),
			_audit_path(config.attribute_value(
				"path", Genode::String<Absolute_path::capacity()>()))
		{ }

		const char* type() override { return "audit"; }

		/***********************
		 ** Directory service **
		 ***********************/

		Genode::Dataspace_capability dataspace(const char *path) override
		{
			_log(__func__, " ", path);
			return _root_dir.dataspace(_expand(path).string());
		}

		void release(char const *path, Dataspace_capability ds) override
		{
			_log(__func__, " ", path);
			return _root_dir.release(_expand(path).string(), ds);
		}

		Open_result open(const char *path, unsigned int mode, Vfs::Vfs_handle **out, Genode::Allocator &alloc) override
		{
			_log(__func__, " ", path, " ", Genode::Hex(mode, Genode::Hex::OMIT_PREFIX, Genode::Hex::PAD));

			Handle *local_handle;
			try { local_handle = new (alloc) Handle(*this, alloc, mode, path); }
			catch (Genode::Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM;  }
			catch (Genode::Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }

			Open_result r = _root_dir.open(
				_expand(path).string(), mode, &local_handle->audit, alloc);

			if (r == OPEN_OK)
				*out = local_handle;
			else
				destroy(alloc, local_handle);
			return r;
		}

		Opendir_result opendir(char const *path, bool create,
	                               Vfs_handle **out, Allocator &alloc) override
		{
			_log(__func__, " ", path, create ? " create " : "");

			Handle *local_handle;
			try { local_handle = new (alloc) Handle(*this, alloc, 0, path); }
			catch (Genode::Out_of_ram)  { return OPENDIR_ERR_OUT_OF_RAM;  }
			catch (Genode::Out_of_caps) { return OPENDIR_ERR_OUT_OF_CAPS; }

			Opendir_result r = _root_dir.opendir(
				_expand(path).string(), create, &local_handle->audit, alloc);

			if (r == OPENDIR_OK)
				*out = local_handle;
			else
				destroy(alloc, local_handle);
			return r;
		}

		void close(Vfs::Vfs_handle *vfs_handle) override
		{
			Handle *h = static_cast<Handle*>(vfs_handle);
			_log(__func__, " ", h->path);
			if (h) {
				h->audit->ds().close(h->audit);
				destroy(h->alloc(), h);
			}
		}

		Stat_result stat(const char *path, Vfs::Directory_service::Stat &buf) override
		{
			_log(__func__, " ", path);
			return _root_dir.stat(_expand(path).string(), buf);
		}

		Unlink_result unlink(const char *path) override
		{
			_log(__func__, " ", path);
			return _root_dir.unlink(_expand(path).string());
		}

		Rename_result rename(const char *from , const char *to) override
		{
			_log(__func__, " ", from, " ", to);
			return _root_dir.rename(_expand(from).string(), _expand(to).string());
		}

		file_size num_dirent(const char *path) override
		{
			return _root_dir.num_dirent(_expand(path).string());
		}

		bool directory(char const *path) override
		{
			return _root_dir.directory(_expand(path).string());
		}

		char const *leaf_path(const char *path) override
		{
			_expanded_path = _expand(path);
			return _root_dir.leaf_path(_expanded_path.string());
		}


		/**********************
		 ** File I/O service **
		 **********************/

		Write_result write(Vfs_handle *vfs_handle,
		                   Const_byte_range_ptr const &src, size_t &out) override
		{
			Handle &h = *static_cast<Handle*>(vfs_handle);
			h.sync_state();
			Write_result const result = h.audit->fs().write(h.audit, src, out);

			if (result == WRITE_OK)
				_log("wrote to ", h.path, " ", out, " / ", src.num_bytes);
			else if (result == WRITE_ERR_WOULD_BLOCK)
				_log("write stalled for ", h.path);
			else
				_log("write failed for ", h.path);

			return result;
		}

		bool queue_read(Vfs_handle *vfs_handle, size_t len) override
		{
			Handle &h = *static_cast<Handle*>(vfs_handle);
			h.sync_state();
			_log(__func__, " ", h.path, " ", len);
			return h.audit->fs().queue_read(h.audit, len);
		}

		Read_result complete_read(Vfs_handle *vfs_handle,
		                          Byte_range_ptr const &dst, size_t &out) override
		{
			Handle &h = *static_cast<Handle*>(vfs_handle);
			h.sync_state();

			Read_result const result = h.audit->fs().complete_read(h.audit, dst, out);

			if (result == READ_OK)
				_log("completed read from ", h.path, " ", out);
			else if (result == READ_QUEUED)
				_log("read queued for ", h.path);
			else
				_log("read error for ", h.path);

			return result;
		}

		bool read_ready(Vfs_handle const &vfs_handle) const override
		{
			Handle const &h = static_cast<Handle const &>(vfs_handle);

			const_cast<Handle &>(h).sync_state();

			return h.audit->fs().read_ready(*h.audit);
		}

		bool write_ready(Vfs_handle const &vfs_handle) const override
		{
			Handle const &h = static_cast<Handle const &>(vfs_handle);

			const_cast<Handle &>(h).sync_state();

			return h.audit->fs().write_ready(*h.audit);
		}

		bool notify_read_ready(Vfs_handle *vfs_handle) override
		{
			Handle &h = *static_cast<Handle*>(vfs_handle);
			h.sync_state();
			return h.audit->fs().notify_read_ready(h.audit);
		}

		Ftruncate_result ftruncate(Vfs_handle *vfs_handle,
		                           file_size len) override
		{
			Handle &h = *static_cast<Handle*>(vfs_handle);
			h.sync_state();
			_log(__func__, " ", h.path, " ", len);
			return h.audit->fs().ftruncate(h.audit, len);
		}

		bool queue_sync(Vfs_handle *vfs_handle) override
		{
			Handle &h = *static_cast<Handle*>(vfs_handle);
			h.sync_state();
			_log("sync ", h.path);
			return h.audit->fs().queue_sync(h.audit);
		}

		Sync_result complete_sync(Vfs_handle *vfs_handle) override
		{
			Handle &h = *static_cast<Handle*>(vfs_handle);
			h.sync_state();

			Sync_result const result = h.audit->fs().complete_sync(h.audit);
			if (result == SYNC_OK)          _log("synced ", h.path);
			if (result == SYNC_ERR_INVALID) _log("sync failed for ", h.path);

			return result;
		}
};


extern "C" Vfs::File_system_factory *vfs_file_system_factory(void)
{
	struct Factory : Vfs::File_system_factory
	{
		Vfs::File_system *create(Vfs::Env &env, Genode::Node const &config) override
		{
			return new (env.alloc())
				Vfs_audit::File_system(env, config);
		}
	};

	static Factory f;
	return &f;
}
