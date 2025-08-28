/*
 * \brief  Native fetchurl utility
 * \author Emery Hemingway
 * \date   2016-03-08
 */

/*
 * Copyright (C) 2016-2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <timer_session/connection.h>
#include <os/path.h>
#include <os/reporter.h>
#include <libc/component.h>
#include <base/heap.h>
#include <base/log.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"

/* cURL includes */
#include <curl/curl.h>

/* libc includes */
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#pragma GCC diagnostic pop  /* restore -Wconversion warnings */

namespace Fetchurl {
	class Fetch;
	struct User_data;
	struct Main;

	using namespace Genode;

	using Url  = String<256>;
	using Path = Path<256>;
}

static size_t write_callback(char   *ptr,
                             size_t  size,
                             size_t  nmemb,
                             void   *userdata);

static int progress_callback(void *userdata,
                             curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow);


class Fetchurl::Fetch : List<Fetch>::Element
{
	friend class List<Fetch>;

	public:

		using List<Fetch>::Element::next;

		Main &main;

		Url  const url;
		Path const path;
		Url  const proxy;
		long       retry;
		bool const head;

		uint64_t dltotal = 0;
		uint64_t dlnow = 0;

		bool timeout = false;

		bool finished = false;
		bool successful = false;

		int fd = -1;

		Fetch(Main &main, Url const &url, Path const &path,
		      Url const &proxy, long retry, bool head)
		:
			main(main), url(url), path(path),
			proxy(proxy), retry(retry+1), head(head)
		{ }
};


struct Fetchurl::User_data
{
	Timer::Connection &timer;
	Milliseconds last_ms;
	Milliseconds const max_timeout;
	Milliseconds curr_timeout;
	Fetchurl::Fetch &fetch;
};


struct Fetchurl::Main
{
	Main(Main const &);
	Main &operator = (Main const &);

	Libc::Env &_env;

	Heap _heap { _env.ram(), _env.rm() };

	Timer::Connection _timer { _env, "reporter" };

	Constructible<Expanding_reporter> _reporter { };

	List<Fetch> _fetches { };

	Timer::One_shot_timeout<Main> _report_timeout {
		_timer, *this, &Main::_report };

	Duration _report_delay { Milliseconds { 0 } };

	Milliseconds _progress_timeout { 10u * 1000 };

	bool _ignore_result { false };

	bool _verbose { false };

	void _schedule_report()
	{
		using namespace Genode;

		if ((_report_delay.trunc_to_plain_ms().value > 0) &&
		    (!_report_timeout.scheduled()))
		{
			_report_timeout.schedule(_report_delay.trunc_to_plain_us());
		}
	}

	void _report()
	{
		if (!_reporter.constructed())
			return;

		_reporter->generate([&] (Generator &g) {
			for (Fetch *f = _fetches.first(); f; f = f->next()) {
				g.node("fetch", [&] {
					g.attribute("url",   f->url);
					g.attribute("total", f->dltotal);
					g.attribute("now",   f->dlnow);
					if (f->timeout) {
						g.attribute("timeout", true);
					}

					g.attribute("finished", f->finished);
					if (f->finished)
						g.attribute("result", f->successful ? "success"
						                                    : "failed");
				});
			}
		});
	}

	void _report(Duration) { _report(); }

	void parse_config(Node const &config_node)
	{
		using namespace Genode;

		enum { DEFAULT_DELAY_MS = 100UL };

		config_node.with_optional_sub_node("report",
			[&] (Node const &report_node) {
				if (report_node.attribute_value("progress", false)) {
					Milliseconds delay_ms { 0 };
					delay_ms.value = report_node.attribute_value(
						"delay_ms", (unsigned)DEFAULT_DELAY_MS);
					if (delay_ms.value < 1)
						delay_ms.value = DEFAULT_DELAY_MS;

					_report_delay = Duration(delay_ms);
					_schedule_report();
					_reporter.construct(_env, "progress", "progress");
				}
			});

		_progress_timeout.value = config_node.attribute_value("progress_timeout",
		                                                      _progress_timeout.value);

		auto const parse_fn = [&] (Node const &node) {

			if (!node.has_attribute("url") || !node.has_attribute("path")) {
				error("error reading 'fetch' config node");
				return;
			}

			Url  const url   = node.attribute_value("url",   Url());
			Path const path  = node.attribute_value("path",  String<256>());
			Url  const proxy = node.attribute_value("proxy", Url());
			long const retry = node.attribute_value("retry", 0L);

			bool const head  = node.attribute_value("head", false);

			auto *f = new (_heap) Fetch(*this, url, path, proxy, retry, head);
			_fetches.insert(f);
		};

		config_node.for_each_sub_node("fetch", parse_fn);

		_ignore_result = config_node.attribute_value("ignore_failures",
		                                             _ignore_result);

		_verbose = config_node.attribute_value("verbose", false);
	}

	Main(Libc::Env &e) : _env(e)
	{
		_env.with_config([&] (Node const &config) {
			parse_config(config);
		});
	}

	CURLcode _process_fetch(CURL *_curl, Fetch &_fetch)
	{
		log("fetch ", _fetch.url, " to ", _fetch.path);

		char const *out_path = _fetch.path.base();

		/* create compound directories leading to the path */
		for (size_t sub_path_len = 0; ; sub_path_len++) {

			bool const end_of_path = (out_path[sub_path_len] == 0);

			bool const end_of_elem = (out_path[sub_path_len] == '/');

			if (end_of_path)
				break;

			if (!end_of_elem)
				continue;

			/* handle leading '/' */
			if (end_of_elem && (sub_path_len == 0))
				continue;

			String<256> sub_path(Cstring(out_path, sub_path_len));

			/* skip '/' */
			sub_path_len++;

			/* if sub path is a directory, we are fine */
			struct stat sb;
			sb.st_mode = 0;
			stat(sub_path.string(), &sb);
			if (S_ISDIR(sb.st_mode))
				continue;

			/* create directory for sub path */
			if (mkdir(sub_path.string(), 0777) < 0) {
				error("failed to create directory ", sub_path);
				return CURLE_FAILED_INIT;
			}
		}

		int fd = open(out_path, O_CREAT | O_RDWR);
		if (fd == -1) {
			switch (errno) {
			case EACCES:
				error("permission denied at ", out_path); break;
			case EEXIST:
				error(out_path, " already exists"); break;
			case EISDIR:
				error(out_path, " is a directory"); break;
			case ENOSPC:
				error("cannot create ", out_path, ", out of space"); break;
			default:
				error("creation of ", out_path, " failed (errno=", errno, ")");
			}
			return CURLE_FAILED_INIT;
		}
		_fetch.fd = fd;

		if (_verbose)
			curl_easy_setopt(_curl, CURLOPT_VERBOSE, 1L);

		curl_easy_setopt(_curl, CURLOPT_URL, _fetch.url.string());
		curl_easy_setopt(_curl, CURLOPT_FOLLOWLOCATION, true);

		curl_easy_setopt(_curl, CURLOPT_NOSIGNAL, true);
		curl_easy_setopt(_curl, CURLOPT_FAILONERROR, 1L);

		curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &_fetch);

		curl_easy_setopt(_curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(_curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
		User_data ud {
			.timer        = _timer,
			.last_ms      = _timer.curr_time().trunc_to_plain_ms(),
			.max_timeout  = _progress_timeout,
			.curr_timeout = Milliseconds { 0 },
			.fetch        = _fetch,
		};
		curl_easy_setopt(_curl, CURLOPT_PROGRESSDATA, &ud);

		curl_easy_setopt(_curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(_curl, CURLOPT_SSL_VERIFYHOST, 0L);

		/* check for optional proxy configuration */
		if (_fetch.proxy != "") {
			curl_easy_setopt(_curl, CURLOPT_PROXY, _fetch.proxy.string());
		}

		curl_easy_setopt(_curl, CURLOPT_USERAGENT, "fetchurl/" LIBCURL_VERSION);

		if (_fetch.head)
			curl_easy_setopt(_curl, CURLOPT_NOBODY, 1L);

		CURLcode res = curl_easy_perform(_curl);
		close(_fetch.fd);
		_fetch.fd = -1;

		if (res != CURLE_OK) {
			unsigned long response_code = -1;
			curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &response_code);
			error(curl_easy_strerror(res), " (code=", response_code, "), failed to fetch ", _fetch.url);
		}
		return res;
	}

	int run()
	{
		CURLcode exit_res = CURLE_OK;

		CURL *curl = curl_easy_init();
		if (!curl) {
			error("failed to initialize libcurl");
			return -1;
		}

		while (true) {
			_report();

			bool retry_some = false;
			for (Fetch *f = _fetches.first(); f; f = f->next()) {
				if (f->retry < 1) continue;
				CURLcode res = _process_fetch(curl, *f);
				if (res == CURLE_OK) {
					f->retry = 0;
					f->finished = true;
					f->successful = true;
				} else {
					if (--f->retry > 0)
						retry_some = true;
					else {
						f->finished = true;
						exit_res = res;
					}
				}
			}
			if (!retry_some) break;
		}

		_report();

		curl_easy_cleanup(curl);

		int const result = exit_res ^ CURLE_OK;
		return _ignore_result ? 0 : result;
	}
};


static size_t write_callback(char   *ptr,
                             size_t  size,
                             size_t  nmemb,
                             void   *userdata)
{
	Fetchurl::Fetch &fetch = *((Fetchurl::Fetch *)userdata);
	return write(fetch.fd, ptr, size*nmemb);
}


static int progress_callback(void *userdata,
                             curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow)
{
	(void)ultotal;
	(void)ulnow;

	using namespace Fetchurl;
	using namespace Genode;

	User_data         &ud    = *reinterpret_cast<User_data*>(userdata);
	Timer::Connection &timer = ud.timer;
	Fetch             &fetch = ud.fetch;

	Milliseconds curr { timer.curr_time().trunc_to_plain_ms() };
	Milliseconds diff { curr.value - ud.last_ms.value };
	ud.last_ms = curr;

	/*
	 * To catch stuck downloads we increase the timeout time whenever
	 * the current download rate is same as the last one. When we hit
	 * the max timeout value, we will abort the download attempt.
	 */

	if ((uint64_t)dlnow == fetch.dlnow) {
		ud.curr_timeout.value += diff.value;
	}
	else {
		ud.curr_timeout.value = 0;
	}
	bool const timeout = ud.curr_timeout.value >= ud.max_timeout.value;

	fetch.dltotal = dltotal;
	fetch.dlnow   = dlnow;
	fetch.timeout = timeout;
	fetch.main._schedule_report();

	/* non-zero return is enough to trigger an abort */
	return timeout ? CURLE_GOT_NOTHING : CURLE_OK;
}


void Libc::Component::construct(Libc::Env &env)
{
	int res = -1;

	Libc::with_libc([&]() {
		curl_global_init(CURL_GLOBAL_DEFAULT);

		static Fetchurl::Main inst(env);
		res = inst.run();

		curl_global_cleanup();
	});

	env.parent().exit(res);
}

/* dummies to prevent warnings printed by unimplemented libc functions */
extern "C" int   issetugid() { return 1; }
extern "C" pid_t getpid()    { return 1; }

