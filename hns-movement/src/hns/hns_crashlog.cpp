#include "hns_crashlog.h"

#ifndef _WIN32
#include <execinfo.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include "tier0/platform.h"
#include "version_gen.h"

namespace hns::crashlog
{
	static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
	static struct sigaction previous[sizeof(signals) / sizeof(signals[0])];
	static bool installed = false;
	static volatile sig_atomic_t handling = 0;
	static char directory[PATH_MAX];

	// Everything below runs inside a signal handler: only async-signal-safe calls (write, open, close, backtrace after
	// it has been called once outside the handler).
	static void WriteAll(int fd, const char *s)
	{
		if (fd < 0)
		{
			return;
		}
		size_t len = strlen(s);
		while (len > 0)
		{
			ssize_t n = write(fd, s, len);
			if (n <= 0)
			{
				return;
			}
			s += n;
			len -= (size_t)n;
		}
	}

	static void WriteNumber(int fd, unsigned long long value, int base)
	{
		char buf[32];
		int i = sizeof(buf) - 1;
		buf[i] = '\0';
		if (value == 0)
		{
			buf[--i] = '0';
		}
		while (value > 0 && i > 0)
		{
			unsigned digit = (unsigned)(value % base);
			buf[--i] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
			value /= base;
		}
		WriteAll(fd, buf + i);
	}

	static const char *SignalName(int sig)
	{
		switch (sig)
		{
			case SIGSEGV:
				return "SIGSEGV (bad memory access)";
			case SIGBUS:
				return "SIGBUS";
			case SIGILL:
				return "SIGILL";
			case SIGFPE:
				return "SIGFPE";
			case SIGABRT:
				return "SIGABRT (abort, e.g. stack smashing)";
		}
		return "signal";
	}

	static void Report(int fd, int sig, siginfo_t *info, void **frames, int count)
	{
		WriteAll(fd, "\n========== hns-movement crash report ==========\n");
		WriteAll(fd, "build: ");
		WriteAll(fd, PLUGIN_FULL_VERSION);
		WriteAll(fd, " (");
		WriteAll(fd, PLUGIN_REVISION_SHORT);
		WriteAll(fd, ")\n");
		WriteAll(fd, "signal: ");
		WriteAll(fd, SignalName(sig));
		WriteAll(fd, "\nfault address: 0x");
		WriteNumber(fd, info ? (unsigned long long)(uintptr_t)info->si_addr : 0, 16);
		WriteAll(fd, "\nstack (module(+offset)):\n");
		backtrace_symbols_fd(frames, count, fd);
		// Module load addresses, so offsets can be computed for frames printed with an absolute address only.
		WriteAll(fd, "modules:\n");
		for (int i = 0; i < count; i++)
		{
			Dl_info dl;
			// dladdr is not formally async-signal-safe, but glibc's only takes a lock that a crashing thread is
			// unlikely to hold; the stack above is already written if it does hang.
			if (dladdr(frames[i], &dl) && dl.dli_fname)
			{
				WriteAll(fd, "  #");
				WriteNumber(fd, (unsigned long long)i, 10);
				WriteAll(fd, " ");
				WriteAll(fd, dl.dli_fname);
				WriteAll(fd, " base 0x");
				WriteNumber(fd, (unsigned long long)(uintptr_t)dl.dli_fbase, 16);
				WriteAll(fd, " offset 0x");
				WriteNumber(fd, (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)dl.dli_fbase), 16);
				WriteAll(fd, "\n");
			}
		}
		WriteAll(fd, "===============================================\n");
	}

	static void Handler(int sig, siginfo_t *info, void *context)
	{
		if (!handling)
		{
			handling = 1;
			void *frames[64];
			int count = backtrace(frames, 64);

			Report(STDERR_FILENO, sig, info, frames, count);

			if (directory[0])
			{
				char path[PATH_MAX];
				size_t len = strlen(directory);
				if (len + 40 < sizeof(path))
				{
					memcpy(path, directory, len);
					memcpy(path + len, "/crash-", 7);
					len += 7;
					// time() is async-signal-safe on Linux (vDSO / syscall).
					unsigned long long now = (unsigned long long)time(nullptr);
					char digits[24];
					int d = 0;
					do
					{
						digits[d++] = (char)('0' + now % 10);
						now /= 10;
					} while (now && d < 23);
					while (d > 0)
					{
						path[len++] = digits[--d];
					}
					memcpy(path + len, ".txt", 5);
					int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					if (fd >= 0)
					{
						Report(fd, sig, info, frames, count);
						close(fd);
					}
				}
			}
		}

		// Hand over to whatever was there before (the game's breakpad handler) and let it run as usual.
		for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
		{
			if (signals[i] == sig)
			{
				sigaction(sig, &previous[i], nullptr);
				break;
			}
		}
		if (sig == SIGABRT || (info && info->si_code <= 0))
		{
			// Sent by abort()/raise(): returning would not repeat it, so send it again.
			raise(sig);
		}
		// A fault (SIGSEGV etc.) repeats on return and now reaches the previous handler.
	}

	void Install()
	{
		if (installed)
		{
			return;
		}
		// Load libgcc's unwinder now: the first backtrace() call may allocate, which is not safe inside a handler.
		void *warmup[2];
		backtrace(warmup, 2);

		snprintf(directory, sizeof(directory), "%s/csgo/cfg/hns-movement", Plat_GetGameDirectory());

		struct sigaction action = {};
		action.sa_sigaction = Handler;
		action.sa_flags = SA_SIGINFO | SA_ONSTACK;
		sigemptyset(&action.sa_mask);
		for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
		{
			sigaction(signals[i], &action, &previous[i]);
		}
		installed = true;
	}

	void Uninstall()
	{
		if (!installed)
		{
			return;
		}
		for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
		{
			struct sigaction current;
			// Only put the old handler back if ours is still the one installed.
			if (sigaction(signals[i], nullptr, &current) == 0 && current.sa_sigaction == Handler)
			{
				sigaction(signals[i], &previous[i], nullptr);
			}
		}
		installed = false;
	}
} // namespace hns::crashlog

#else

namespace hns::crashlog
{
	void Install() {}

	void Uninstall() {}
} // namespace hns::crashlog

#endif
