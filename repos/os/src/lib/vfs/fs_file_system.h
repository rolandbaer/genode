/*
 * \brief  Adapter from Genode 'File_system' session to VFS
 * \author Norman Feske
 * \author Emery Hemingway
 * \author Christian Helmuth
 * \date   2011-02-17
 */

/*
 * Copyright (C) 2012-2019 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__VFS__FS_FILE_SYSTEM_H_
#define _INCLUDE__VFS__FS_FILE_SYSTEM_H_

/* Genode includes */
#include <base/allocator_avl.h>
#include <base/id_space.h>
#include <file_system_session/connection.h>

namespace Vfs { class Fs_file_system; }


class Vfs::Fs_file_system : public File_system, private Remote_io
{
	private:

		Vfs::Env              &_env;
		Genode::Allocator_avl  _fs_packet_alloc { &_env.alloc() };

		using Label_string = Genode::String<64>;
		Label_string _label;

		::File_system::Connection _fs;

		bool _write_would_block = false;

		using Handle_space = Genode::Id_space<::File_system::Node>;

		Handle_space _handle_space { };
		Handle_space _watch_handle_space { };

		struct Handle_state
		{
			enum class Read_ready_state { IDLE, PENDING, READY };
			Read_ready_state read_ready_state = Read_ready_state::IDLE;

			enum class Queued_state { IDLE, QUEUED, ACK };
			Queued_state queued_read_state = Queued_state::IDLE;
			Queued_state queued_sync_state = Queued_state::IDLE;

			::File_system::Packet_descriptor queued_read_packet { };
			::File_system::Packet_descriptor queued_sync_packet { };
		};

		struct Fs_vfs_handle;
		using Fs_vfs_handle_queue = Genode::Fifo<Fs_vfs_handle>;

		Remote_io::Peer _peer { _env.deferred_wakeups(), *this };

		/**
		 * Remote_io interface
		 */
		void wakeup_remote_peer() override { _fs.tx()->wakeup(); }

		/*
		 * Pass packet to server side
		 *
		 * The caller is expected to check 'ready_to_submit' before calling
		 * this function.
		 */
		void _submit_packet(::File_system::Packet_descriptor const &packet)
		{
			/*
			 * The warning should never occur if the precondition above is
			 * satisfied.
			 */
			if (!_fs.tx()->ready_to_submit())
				Genode::warning("submit queue of file-system session unexpectedly full");
			else
				_fs.tx()->try_submit_packet(packet);

			_peer.schedule_wakeup();
		}

		/**
		 * Convert 'File_system::Node_type' to 'Dirent_type'
		 */
		static Dirent_type _dirent_type(::File_system::Node_type type)
		{
			using ::File_system::Node_type;

			switch (type) {
			case Node_type::DIRECTORY:          return Dirent_type::DIRECTORY;
			case Node_type::CONTINUOUS_FILE:    return Dirent_type::CONTINUOUS_FILE;
			case Node_type::TRANSACTIONAL_FILE: return Dirent_type::TRANSACTIONAL_FILE;
			case Node_type::SYMLINK:            return Dirent_type::SYMLINK;
			}
			return Dirent_type::END;
		}

		/**
		 * Convert 'File_system::Node_type' to 'Vfs::Node_type'
		 */
		static Node_type _node_type(::File_system::Node_type type)
		{
			using Type = ::File_system::Node_type;

			switch (type) {
			case Type::DIRECTORY:          return Node_type::DIRECTORY;
			case Type::CONTINUOUS_FILE:    return Node_type::CONTINUOUS_FILE;
			case Type::TRANSACTIONAL_FILE: return Node_type::TRANSACTIONAL_FILE;
			case Type::SYMLINK:            return Node_type::SYMLINK;
			}

			Genode::error("invalid File_system::Node_type");
			return Node_type::CONTINUOUS_FILE;
		}

		/**
		 * Convert 'File_system::Node_rwx' to 'Vfs::Node_rwx'
		 */
		static Node_rwx _node_rwx(::File_system::Node_rwx rwx)
		{
			return { .readable   = rwx.readable,
			         .writeable  = rwx.writeable,
			         .executable = rwx.executable };
		}

		struct Fs_vfs_handle : Vfs_handle,
		                       private ::File_system::Node,
		                       private Handle_space::Element,
		                       private Handle_state
		{
			friend Genode::Id_space<::File_system::Node>;
			friend Fs_vfs_handle_queue;

			using Handle_state::queued_read_state;
			using Handle_state::queued_read_packet;
			using Handle_state::queued_sync_packet;
			using Handle_state::queued_sync_state;
			using Handle_state::read_ready_state;

			Fs_file_system &_vfs_fs;

			bool _queue_read(size_t count, file_size const seek_offset)
			{
				if (queued_read_state != Handle_state::Queued_state::IDLE)
					return false;

				::File_system::Session::Tx::Source &source = *_vfs_fs._fs.tx();

				/* if not ready to submit suggest retry */
				if (!source.ready_to_submit())
					return false;

				size_t const max_packet_size = source.bulk_buffer_size() / 2;
				size_t const clipped_count = min(max_packet_size, count);

				::File_system::Packet_descriptor p;
				try {
					p = source.alloc_packet((size_t)clipped_count);
				} catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					return false;
				}

				::File_system::Packet_descriptor const
					packet(p, file_handle(),
					       ::File_system::Packet_descriptor::READ,
					       (size_t)clipped_count, seek_offset);

				read_ready_state  = Handle_state::Read_ready_state::IDLE;
				queued_read_state = Handle_state::Queued_state::QUEUED;

				/* pass packet to server side */
				_vfs_fs._submit_packet(packet);

				return true;
			}

			Read_result _complete_read(Byte_range_ptr const &dst, size_t &out_count)
			{
				if (queued_read_state != Handle_state::Queued_state::ACK)
					return READ_QUEUED;

				::File_system::Session::Tx::Source &source = *_vfs_fs._fs.tx();

				/* obtain result packet descriptor with updated status info */
				::File_system::Packet_descriptor const
					packet = queued_read_packet;

				Read_result result = packet.succeeded() ? READ_OK : READ_ERR_IO;

				if (result == READ_OK) {
					size_t const read_num_bytes = min(packet.length(), dst.num_bytes);

					memcpy(dst.start, source.packet_content(packet), (size_t)read_num_bytes);

					out_count = read_num_bytes;
				}

				queued_read_state  = Handle_state::Queued_state::IDLE;
				queued_read_packet = ::File_system::Packet_descriptor();

				source.release_packet(packet);

				return result;
			}

			Fs_vfs_handle(File_system &fs, Allocator &alloc,
			              int status_flags, Handle_space &space,
			              ::File_system::Node_handle node_handle,
			              Fs_file_system &vfs_fs)
			:
				Vfs_handle(fs, fs, alloc, status_flags),
				Handle_space::Element(*this, space, node_handle),
				_vfs_fs(vfs_fs)
			{ }

			::File_system::File_handle file_handle() const
			{ return ::File_system::File_handle { id().value }; }

			virtual bool queue_read(size_t /* count */)
			{
				Genode::error("Fs_vfs_handle::queue_read() called");
				return true;
			}

			virtual Read_result complete_read(Byte_range_ptr const &,
			                                  size_t & /* out count */)
			{
				Genode::error("Fs_vfs_handle::complete_read() called");
				return READ_ERR_INVALID;
			}

			bool queue_sync()
			{
				if (queued_sync_state != Handle_state::Queued_state::IDLE)
					return true;

				::File_system::Session::Tx::Source &source = *_vfs_fs._fs.tx();

				/* if not ready to submit suggest retry */
				if (!source.ready_to_submit()) return false;

				::File_system::Packet_descriptor p;
				try {
					p = source.alloc_packet(0);
				} catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					return false;
				}

				::File_system::Packet_descriptor const
					packet(p, file_handle(),
					       ::File_system::Packet_descriptor::SYNC, 0);

				queued_sync_state = Handle_state::Queued_state::QUEUED;

				/* pass packet to server side */
				_vfs_fs._submit_packet(packet);

				return true;
			}

			Sync_result complete_sync()
			{
				if (queued_sync_state != Handle_state::Queued_state::ACK)
					return SYNC_QUEUED;

				/* obtain result packet descriptor */
				::File_system::Packet_descriptor const
					packet = queued_sync_packet;

				::File_system::Session::Tx::Source &source = *_vfs_fs._fs.tx();

				Sync_result result = packet.succeeded()
					? SYNC_OK : SYNC_ERR_INVALID;

				queued_sync_state  = Handle_state::Queued_state::IDLE;
				queued_sync_packet = ::File_system::Packet_descriptor();

				source.release_packet(packet);

				return result;
			}

			bool update_modification_timestamp(Vfs::Timestamp time)
			{
				::File_system::Session::Tx::Source &source = *_vfs_fs._fs.tx();
				using ::File_system::Packet_descriptor;

				if (!source.ready_to_submit()) {
					return false;
				}

				try {
					Packet_descriptor p(source.alloc_packet(0),
					                    file_handle(),
					                    Packet_descriptor::WRITE_TIMESTAMP,
					                    ::File_system::Timestamp {
					                       .ms_since_1970 = time.ms_since_1970 });

					_vfs_fs._submit_packet(p);
				} catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
					return false;
				}

				return true;
			}
		};

		struct Fs_vfs_file_handle : Fs_vfs_handle
		{
			using Fs_vfs_handle::Fs_vfs_handle;

			bool queue_read(size_t count) override
			{
				return _queue_read(count, seek());
			}

			Read_result complete_read(Byte_range_ptr const &dst, size_t &out_count) override
			{
				return _complete_read(dst, out_count);
			}
		};

		struct Fs_vfs_dir_handle : Fs_vfs_handle
		{
			enum { DIRENT_SIZE = sizeof(::File_system::Directory_entry) };

			using Fs_vfs_handle::Fs_vfs_handle;

			bool queue_read(size_t count) override
			{
				if (count < sizeof(Dirent))
					return true;

				return _queue_read(DIRENT_SIZE,
				                   (seek() / sizeof(Dirent) * DIRENT_SIZE));
			}

			Read_result complete_read(Byte_range_ptr const &dst, size_t &out_count) override
			{
				if (dst.num_bytes < sizeof(Dirent))
					return READ_ERR_INVALID;

				using ::File_system::Directory_entry;

				Directory_entry entry { };

				size_t entry_out_count = 0;

				Read_result const read_result =
					_complete_read(Byte_range_ptr((char *)(&entry), DIRENT_SIZE),
					               entry_out_count);

				if (read_result != READ_OK)
					return read_result;

				entry.sanitize();

				Dirent &dirent = *(Dirent*)dst.start;

				if (entry_out_count < DIRENT_SIZE) {

					/* no entry found for the given index, or error */
					dirent = Dirent {
						.fileno = 0,
						.type   = Dirent_type::END,
						.rwx    = { },
						.name   = { }
					};
					out_count = sizeof(Dirent);
					return READ_OK;
				}

				dirent = Dirent {
					.fileno = entry.inode,
					.type   = _dirent_type(entry.type),
					.rwx    = _node_rwx(entry.rwx),
					.name   = { entry.name.buf }
				};

				out_count = sizeof(Dirent);

				return READ_OK;
			}
		};

		struct Fs_vfs_symlink_handle : Fs_vfs_handle
		{
			using Fs_vfs_handle::Fs_vfs_handle;

			bool queue_read(size_t count) override
			{
				return _queue_read(count, seek());
			}

			Read_result complete_read(Byte_range_ptr const &dst,
			                          size_t &out_count) override
			{
				return _complete_read(dst, out_count);
			}
		};

		/**
		 * Helper for managing the lifetime of temporary open node handles
		 */
		struct Fs_handle_guard : Fs_vfs_handle
		{
			Fs_handle_guard(File_system &fs,
			                ::File_system::Node_handle fs_handle,
			                Handle_space &space,
			                Fs_file_system &vfs_fs)
			:
				Fs_vfs_handle(fs, *(Allocator*)nullptr, 0, space, fs_handle, vfs_fs)
			{ }

			~Fs_handle_guard()
			{
				_vfs_fs._fs.close(file_handle());
			}
		};

		struct Fs_vfs_watch_handle final : Vfs_watch_handle,
		                                   private ::File_system::Node,
		                                   private Handle_space::Element
		{
			friend Genode::Id_space<::File_system::Node>;

			::File_system::Watch_handle const  fs_handle;

			Fs_vfs_watch_handle(Vfs::File_system            &fs,
			                    Allocator                   &alloc,
			                    Handle_space                &space,
			                    ::File_system::Watch_handle  handle)
			:
				Vfs_watch_handle(fs, alloc),
				Handle_space::Element(*this, space, handle),
				fs_handle(handle)
			{ }
		};

		Write_result _write(Fs_vfs_handle &handle, file_size const seek_offset,
		                    Const_byte_range_ptr const &src, size_t &out_count)
		{
			/* reclaim as much space in the packet stream as possible */
			_handle_ack();

			::File_system::Session::Tx::Source &source = *_fs.tx();
			using ::File_system::Packet_descriptor;

			size_t const max_packet_size = source.bulk_buffer_size() / 2;
			size_t const count = min(max_packet_size, src.num_bytes);

			if (!source.ready_to_submit()) {
				_write_would_block = true;
				return Write_result::WRITE_ERR_WOULD_BLOCK;
			}

			try {
				Packet_descriptor packet_in(source.alloc_packet(count),
				                            handle.file_handle(),
				                            Packet_descriptor::WRITE,
				                            count,
				                            seek_offset);

				memcpy(source.packet_content(packet_in), src.start, count);

				_submit_packet(packet_in);
			}
			catch (::File_system::Session::Tx::Source::Packet_alloc_failed) {
				_write_would_block = true;
				return Write_result::WRITE_ERR_WOULD_BLOCK;
			}
			catch (...) {
				Genode::error("unhandled exception");
				return Write_result::WRITE_ERR_IO;
			}
			out_count = count;
			return Write_result::WRITE_OK;
		}

		void _handle_ack()
		{
			::File_system::Session::Tx::Source &source = *_fs.tx();
			using ::File_system::Packet_descriptor;

			bool any_ack_handled = false;

			while (source.ack_avail()) {

				Packet_descriptor const packet = source.try_get_acked_packet();
				_write_would_block = false;
				_peer.schedule_wakeup();

				Handle_space::Id const id(packet.handle());

				auto handle_fn = [&] (Fs_vfs_handle &handle)
				{
					if (!packet.succeeded())
						Genode::error("packet operation=", (int)packet.operation(), " failed");

					switch (packet.operation()) {
					case Packet_descriptor::READ_READY:
						handle.read_ready_state = Handle_state::Read_ready_state::READY;
						handle.read_ready_response();
						break;

					case Packet_descriptor::READ:
						handle.queued_read_packet = packet;
						handle.queued_read_state  = Handle_state::Queued_state::ACK;
						break;

					case Packet_descriptor::WRITE:
						source.release_packet(packet);
						break;

					case Packet_descriptor::SYNC:
						handle.queued_sync_packet = packet;
						handle.queued_sync_state  = Handle_state::Queued_state::ACK;
						break;

					case Packet_descriptor::CONTENT_CHANGED:
						break;

					case Packet_descriptor::WRITE_TIMESTAMP:
						source.release_packet(packet);
						break;
					}
				};

				try {
					if (packet.operation() == Packet_descriptor::CONTENT_CHANGED) {
						_watch_handle_space.apply<Fs_vfs_watch_handle>(id, [&] (Fs_vfs_watch_handle &handle) {
							handle.watch_response(); });
					} else {
						_handle_space.apply<Fs_vfs_handle>(id, handle_fn);
					}
				}
				catch (Handle_space::Unknown_id) {
					Genode::warning("ack for unknown File_system handle ", id); }

				if (packet.succeeded())
					any_ack_handled = true;
			}

			if (any_ack_handled)
				_env.user().wakeup_vfs_user();
		}

		Genode::Io_signal_handler<Fs_file_system> _signal_handler {
			_env.env().ep(), *this, &Fs_file_system::_handle_ack };

		static size_t buffer_size(Node const &config)
		{
			Genode::Number_of_bytes fs_default { ::File_system::DEFAULT_TX_BUF_SIZE };
			return config.attribute_value("buffer_size", fs_default);
		}

	public:

		Fs_file_system(Vfs::Env &env, Node const &config)
		:
			_env(env),
			_label(config.attribute_value("label", Label_string("/"))),
			_fs(_env.env(), _fs_packet_alloc,
			    _label,
			    config.attribute_value("writeable", true),
			    buffer_size(config))
		{
			if (config.has_attribute("root")) {
				Genode::warning("vfs: <fs> node uses deprecated 'root' attribute.");
				Genode::warning("     Append the root dir to the label instead.");
			}

			_fs.sigh(_signal_handler);
		}

		/*********************************
		 ** Directory-service interface **
		 *********************************/

		Dataspace_capability dataspace(char const *) override
		{
			/* cannot be implemented without blocking */
			return Dataspace_capability();
		}

		void release(char const *, Dataspace_capability) override { }

		Stat_result stat(char const *path, Stat &out) override
		{
			::File_system::Status status;

			try {
				::File_system::Node_handle node = _fs.node(path);
				Fs_handle_guard node_guard(*this, node, _handle_space, *this);
				status = _fs.status(node);
			}
			catch (Genode::Out_of_ram)  {
				Genode::error("out-of-ram during stat");
				return STAT_ERR_NO_PERM;
			}
			catch (Genode::Out_of_caps) {
				Genode::error("out-of-caps during stat");
				return STAT_ERR_NO_PERM;
			}
			catch (...) { return STAT_ERR_NO_ENTRY; }

			out = Stat();

			out.size   = status.size;
			out.type   = _node_type(status.type);
			out.rwx    = _node_rwx(status.rwx);
			out.inode  = status.inode;
			out.device = (Genode::addr_t)this;
			out.modification_time = {
				.ms_since_1970 = status.modification_time.ms_since_1970 };

			return STAT_OK;
		}

		Unlink_result unlink(char const *path) override
		{
			Absolute_path dir_path(path);
			dir_path.strip_last_element();

			Absolute_path file_name(path);
			file_name.keep_only_last_element();

			try {
				::File_system::Dir_handle dir = _fs.dir(dir_path.base(), false);
				Fs_handle_guard dir_guard(*this, dir, _handle_space, *this);

				_fs.unlink(dir, file_name.base() + 1);
			}
			catch (::File_system::Invalid_handle)    { return UNLINK_ERR_NO_ENTRY;  }
			catch (::File_system::Invalid_name)      { return UNLINK_ERR_NO_ENTRY;  }
			catch (::File_system::Lookup_failed)     { return UNLINK_ERR_NO_ENTRY;  }
			catch (::File_system::Not_empty)         { return UNLINK_ERR_NOT_EMPTY; }
			catch (::File_system::Permission_denied) { return UNLINK_ERR_NO_PERM;   }
			catch (::File_system::Unavailable)       { return UNLINK_ERR_NO_ENTRY;  }

			return UNLINK_OK;
		}

		Rename_result rename(char const *from_path, char const *to_path) override
		{
			if ((strcmp(from_path, to_path) == 0) && leaf_path(from_path))
				return RENAME_OK;

			Absolute_path from_dir_path(from_path);
			from_dir_path.strip_last_element();

			Absolute_path from_file_name(from_path);
			from_file_name.keep_only_last_element();

			Absolute_path to_dir_path(to_path);
			to_dir_path.strip_last_element();

			Absolute_path to_file_name(to_path);
			to_file_name.keep_only_last_element();

			try {
				::File_system::Dir_handle from_dir =
					_fs.dir(from_dir_path.base(), false);

				Fs_handle_guard from_dir_guard(*this, from_dir, _handle_space, *this);

				::File_system::Dir_handle to_dir = _fs.dir(to_dir_path.base(),
				                                           false);
				Fs_handle_guard to_dir_guard(
					*this, to_dir, _handle_space, *this);

				_fs.move(from_dir, from_file_name.base() + 1,
				         to_dir,   to_file_name.base() + 1);
			}
			catch (::File_system::Lookup_failed) { return RENAME_ERR_NO_ENTRY; }
			catch (...)                          { return RENAME_ERR_NO_PERM; }

			return RENAME_OK;
		}

		file_size num_dirent(char const *path) override
		{
			if (strcmp(path, "") == 0)
				path = "/";

			try {
				::File_system::Dir_handle dir = _fs.dir(path, false);
				Fs_handle_guard node_guard(*this, dir, _handle_space, *this);

				return _fs.num_entries(dir);
			}
			catch (...) { }
			return 0;
		}

		bool directory(char const *path) override
		{
			try {
				::File_system::Node_handle node = _fs.node(path);
				Fs_handle_guard node_guard(*this, node, _handle_space, *this);

				::File_system::Status status = _fs.status(node);

				return status.directory();
			}
			catch (...) { }
			return false;
		}

		char const *leaf_path(char const *path) override
		{
			/* check if node at path exists within file system */
			try {
				::File_system::Node_handle node = _fs.node(path);
				_fs.close(node);
			}
			catch (...) { return 0; }

			return path;
		}

		Open_result open(char const *path, unsigned vfs_mode, Vfs_handle **out_handle,
		                 Genode::Allocator& alloc) override
		{
			Absolute_path dir_path(path);
			dir_path.strip_last_element();

			Absolute_path file_name(path);
			file_name.keep_only_last_element();

			::File_system::Mode mode;

			switch (vfs_mode & OPEN_MODE_ACCMODE) {
			default:               mode = ::File_system::STAT_ONLY;  break;
			case OPEN_MODE_RDONLY: mode = ::File_system::READ_ONLY;  break;
			case OPEN_MODE_WRONLY: mode = ::File_system::WRITE_ONLY; break;
			case OPEN_MODE_RDWR:   mode = ::File_system::READ_WRITE; break;
			}

			bool const create = vfs_mode & OPEN_MODE_CREATE;

			try {
				::File_system::Dir_handle dir = _fs.dir(dir_path.base(), false);
				Fs_handle_guard dir_guard(*this, dir, _handle_space, *this);

				::File_system::File_handle file = _fs.file(dir,
				                                           file_name.base() + 1,
				                                           mode, create);

				*out_handle = new (alloc)
					Fs_vfs_file_handle(*this, alloc, vfs_mode, _handle_space, file, *this);
			}
			catch (::File_system::Lookup_failed)       { return OPEN_ERR_UNACCESSIBLE;  }
			catch (::File_system::Permission_denied)   { return OPEN_ERR_NO_PERM;       }
			catch (::File_system::Invalid_handle)      { return OPEN_ERR_UNACCESSIBLE;  }
			catch (::File_system::Node_already_exists) { return OPEN_ERR_EXISTS;        }
			catch (::File_system::Invalid_name)        { return OPEN_ERR_NAME_TOO_LONG; }
			catch (::File_system::Name_too_long)       { return OPEN_ERR_NAME_TOO_LONG; }
			catch (::File_system::No_space)            { return OPEN_ERR_NO_SPACE;      }
			catch (::File_system::Unavailable)         { return OPEN_ERR_UNACCESSIBLE;  }
			catch (Genode::Out_of_ram)  { return OPEN_ERR_OUT_OF_RAM; }
			catch (Genode::Out_of_caps) { return OPEN_ERR_OUT_OF_CAPS; }

			return OPEN_OK;
		}

		Opendir_result opendir(char const *path, bool create,
		                       Vfs_handle **out_handle, Allocator &alloc) override
		{
			Absolute_path dir_path(path);

			try {
				::File_system::Dir_handle dir = _fs.dir(dir_path.base(), create);

				*out_handle = new (alloc)
					Fs_vfs_dir_handle(*this, alloc, ::File_system::READ_ONLY,
					                  _handle_space, dir, *this);
			}
			catch (::File_system::Lookup_failed)       { return OPENDIR_ERR_LOOKUP_FAILED;       }
			catch (::File_system::Name_too_long)       { return OPENDIR_ERR_NAME_TOO_LONG;       }
			catch (::File_system::Node_already_exists) { return OPENDIR_ERR_NODE_ALREADY_EXISTS; }
			catch (::File_system::No_space)            { return OPENDIR_ERR_NO_SPACE;            }
			catch (::File_system::Permission_denied)   { return OPENDIR_ERR_PERMISSION_DENIED;   }
			catch (Genode::Out_of_ram)  { return OPENDIR_ERR_OUT_OF_RAM; }
			catch (Genode::Out_of_caps) { return OPENDIR_ERR_OUT_OF_CAPS; }

			return OPENDIR_OK;
		}

		Openlink_result openlink(char const *path, bool create,
		                         Vfs_handle **out_handle, Allocator &alloc) override
		{
			/*
			 * Canonicalize path (i.e., path must start with '/')
			 */
			Absolute_path abs_path(path);
			abs_path.strip_last_element();

			Absolute_path symlink_name(path);
			symlink_name.keep_only_last_element();

			try {
				::File_system::Dir_handle dir_handle = _fs.dir(abs_path.base(),
				                                               false);

				Fs_handle_guard from_dir_guard(*this, dir_handle, _handle_space, *this);

				::File_system::Symlink_handle symlink_handle =
				    _fs.symlink(dir_handle, symlink_name.base() + 1, create);

				*out_handle = new (alloc)
					Fs_vfs_symlink_handle(*this, alloc,
					                      ::File_system::READ_ONLY,
					                      _handle_space, symlink_handle, *this);

				return OPENLINK_OK;
			}
			catch (::File_system::Invalid_handle)      { return OPENLINK_ERR_LOOKUP_FAILED; }
			catch (::File_system::Invalid_name)        { return OPENLINK_ERR_LOOKUP_FAILED; }
			catch (::File_system::Lookup_failed)       { return OPENLINK_ERR_LOOKUP_FAILED; }
			catch (::File_system::Node_already_exists) { return OPENLINK_ERR_NODE_ALREADY_EXISTS; }
			catch (::File_system::No_space)            { return OPENLINK_ERR_NO_SPACE; }
			catch (::File_system::Permission_denied)   { return OPENLINK_ERR_PERMISSION_DENIED; }
			catch (::File_system::Unavailable)         { return OPENLINK_ERR_LOOKUP_FAILED; }
			catch (Genode::Out_of_ram)  { return OPENLINK_ERR_OUT_OF_RAM; }
			catch (Genode::Out_of_caps) { return OPENLINK_ERR_OUT_OF_CAPS; }
		}

		void close(Vfs_handle *vfs_handle) override
		{
			Fs_vfs_handle *fs_handle = static_cast<Fs_vfs_handle *>(vfs_handle);

			_fs.close(fs_handle->file_handle());
			destroy(fs_handle->alloc(), fs_handle);
		}

		Watch_result watch(char const      *path,
		                   Vfs_watch_handle **handle,
		                   Allocator        &alloc) override
		{
			using namespace ::File_system;

			Watch_result res = WATCH_ERR_UNACCESSIBLE;
			::File_system::Watch_handle fs_handle { -1UL };

			try { fs_handle = _fs.watch(path); }
			catch (Unavailable)       { return WATCH_ERR_UNACCESSIBLE; }
			catch (Lookup_failed)     { return WATCH_ERR_UNACCESSIBLE; }
			catch (Permission_denied) { return WATCH_ERR_STATIC; }
			catch (Out_of_ram)        { return WATCH_ERR_OUT_OF_RAM; }
			catch (Out_of_caps)       { return WATCH_ERR_OUT_OF_CAPS; }

			try {
				*handle = new (alloc)
					Fs_vfs_watch_handle(
						*this, alloc, _watch_handle_space, fs_handle);
				return WATCH_OK;
			}
			catch (Out_of_ram)  { res = WATCH_ERR_OUT_OF_RAM;  }
			catch (Out_of_caps) { res = WATCH_ERR_OUT_OF_CAPS; }
			_fs.close(fs_handle);
			return res;
		}

		void close(Vfs_watch_handle *vfs_handle) override
		{
			Fs_vfs_watch_handle *handle =
				static_cast<Fs_vfs_watch_handle *>(vfs_handle);
			_fs.close(handle->fs_handle);
			destroy(handle->alloc(), handle);
		};


		/***************************
		 ** File_system interface **
		 ***************************/

		static char const *name()   { return "fs"; }
		char const *type() override { return "fs"; }


		/********************************
		 ** File I/O service interface **
		 ********************************/

		Write_result write(Vfs_handle *vfs_handle, Const_byte_range_ptr const &src,
		                   size_t &out_count) override
		{
			Fs_vfs_handle &handle = static_cast<Fs_vfs_handle &>(*vfs_handle);

			return _write(handle, handle.seek(), src, out_count);
		}

		bool queue_read(Vfs_handle *vfs_handle, size_t count) override
		{
			Fs_vfs_handle *handle = static_cast<Fs_vfs_handle *>(vfs_handle);

			return handle->queue_read(count);
		}

		Read_result complete_read(Vfs_handle *vfs_handle, Byte_range_ptr const &dst,
		                          size_t &out_count) override
		{
			out_count = 0;

			Fs_vfs_handle *handle = static_cast<Fs_vfs_handle *>(vfs_handle);

			return handle->complete_read(dst, out_count);
		}

		bool read_ready(Vfs_handle const &vfs_handle) const override
		{
			Fs_vfs_handle const &handle = static_cast<Fs_vfs_handle const &>(vfs_handle);

			return handle.read_ready_state == Handle_state::Read_ready_state::READY;
		}

		bool write_ready(Vfs_handle const &) const override
		{
			return !_write_would_block;
		}

		bool notify_read_ready(Vfs_handle *vfs_handle) override
		{
			Fs_vfs_handle *handle = static_cast<Fs_vfs_handle *>(vfs_handle);
			if (handle->read_ready_state != Handle_state::Read_ready_state::IDLE)
				return true;

			::File_system::Session::Tx::Source &source = *_fs.tx();

			/* if not ready to submit suggest retry */
			if (!source.ready_to_submit()) return false;

			using ::File_system::Packet_descriptor;

			Packet_descriptor packet(Packet_descriptor(),
			                         handle->file_handle(),
			                         Packet_descriptor::READ_READY,
			                         0, 0);

			handle->read_ready_state = Handle_state::Read_ready_state::PENDING;

			_submit_packet(packet);

			/*
			 * When the packet is acknowledged the application is notified via
			 * Response_handler::handle_response().
			 */
			return true;
		}

		Ftruncate_result ftruncate(Vfs_handle *vfs_handle, file_size len) override
		{
			Fs_vfs_handle const *handle = static_cast<Fs_vfs_handle *>(vfs_handle);

			try {
				_fs.truncate(handle->file_handle(), len);
			}
			catch (::File_system::Invalid_handle)    { return FTRUNCATE_ERR_NO_PERM; }
			catch (::File_system::Permission_denied) { return FTRUNCATE_ERR_NO_PERM; }
			catch (::File_system::No_space)          { return FTRUNCATE_ERR_NO_SPACE; }
			catch (::File_system::Unavailable)       { return FTRUNCATE_ERR_NO_PERM; }

			return FTRUNCATE_OK;
		}

		bool queue_sync(Vfs_handle *vfs_handle) override
		{
			Fs_vfs_handle *handle = static_cast<Fs_vfs_handle *>(vfs_handle);

			return handle->queue_sync();
		}

		Sync_result complete_sync(Vfs_handle *vfs_handle) override
		{
			Fs_vfs_handle *handle = static_cast<Fs_vfs_handle *>(vfs_handle);

			return handle->complete_sync();
		}

		bool update_modification_timestamp(Vfs_handle *vfs_handle, Vfs::Timestamp time) override
		{
			Fs_vfs_handle *handle = static_cast<Fs_vfs_handle *>(vfs_handle);

			return handle->update_modification_timestamp(time);
		}
};

#endif /* _INCLUDE__VFS__FS_FILE_SYSTEM_H_ */
