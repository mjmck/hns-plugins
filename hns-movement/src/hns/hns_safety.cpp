#include "hns_safety.h"
#include "hns_config.h"
#include "utils/utils.h"
#include "hns_netmsg.h"

#include "networksystem/inetworkmessages.h"

#include <string>
#include <vector>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "tier0/memdbgon.h"

using hns::config::settings;

namespace hns::safety
{
	static_global bool safeMode {};
	static_global std::string safeModeReason;
	static_global bool selfTestPassed {};
	static_global bool selfTestRan {};
	static_global bool breakerTripped {};
	// Breaker state for the running CS2 version.
	static_global bool probing {};
	// "probing" is only written once a human is in the game, so restarting an empty server never trips the breaker.
	static_global bool probingWritten {};
	static_global u32 runningVersion {};
	static_global f64 confirmedSeconds {};
	static_global f64 lastFrameTime {};

	bool SafeRead(const void *addr, void *out, size_t n)
	{
		if (!addr)
		{
			return false;
		}
#ifdef _WIN32
		SIZE_T read = 0;
		return ReadProcessMemory(GetCurrentProcess(), addr, out, n, &read) && read == n;
#else
		// write() from an unreadable address fails with EFAULT instead of crashing, and needs no ptrace permission
		// (process_vm_readv can be blocked by a container's seccomp profile).
		static_persist int fds[2] = {-1, -1};
		if (fds[0] == -1 && pipe(fds) != 0)
		{
			fds[0] = fds[1] = -1;
			return false;
		}
		if (n > 512)
		{
			return false;
		}
		ssize_t written = write(fds[1], addr, n);
		if (written <= 0)
		{
			return false;
		}
		// Always drain what was written, so a partial copy never leaves bytes behind for the next read.
		ssize_t got = read(fds[0], out, written);
		return written == (ssize_t)n && got == written;
#endif
	}

	// The messages the plugin sends (chat, centre text, sounds, cvar replication). hns::netmsg checks each one with
	// reads that cannot crash. A message that is missing only disables that feature (sends are skipped); a message
	// whose record does not look like NetMessageInfo_t means the engine layout changed, and every send would call
	// garbage, so that switches the plugin off.
	static_function bool SelfTest(char *reason, size_t maxlen)
	{
		hns::netmsg::ResolveAll();
		for (i32 i = 0; i < hns::netmsg::COUNT; i++)
		{
			if (hns::netmsg::LayoutBroken((hns::netmsg::Message)i, reason, maxlen))
			{
				return false;
			}
		}
		hns::netmsg::PrintSummary();
		return true;
	}

	static_function bool ReadRunState(u32 &version, std::string &state)
	{
		std::vector<char> buffer;
		if (!utils::ReadBufferFromFile(HNS_RUNSTATE_PATH, buffer) || buffer.empty())
		{
			return false;
		}
		std::string text(buffer.begin(), buffer.end());
		// "<version> <state>" on the first line. (No sscanf: newer glibc headers redirect it to a symbol the Steam Runtime lacks.)
		const char *p = text.c_str();
		char *end = nullptr;
		unsigned long v = strtoul(p, &end, 10);
		if (end == p)
		{
			return false;
		}
		while (*end == ' ' || *end == '\t')
		{
			end++;
		}
		const char *stateStart = end;
		while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n')
		{
			end++;
		}
		if (end == stateStart)
		{
			return false;
		}
		version = (u32)v;
		state.assign(stateStart, end - stateStart);
		return true;
	}

	static_function void WriteRunState(u32 version, const char *state)
	{
		char text[128];
		int len = snprintf(text, sizeof(text),
						   "%u %s\n// hns-movement crash breaker (hns_safety.h). Delete this file or run hns_safety_reset to clear it.\n",
						   version, state);
		std::vector<char> buffer(text, text + len);
		utils::WriteBufferToFile(HNS_RUNSTATE_PATH, buffer);
	}

	bool CheckBeforeHooks(char *reason, size_t maxlen)
	{
		reason[0] = '\0';
		runningVersion = utils::GetServerVersion();

		if (!settings.safety)
		{
			probing = false;
			return true;
		}

		u32 lastVersion = 0;
		std::string lastState;
		bool haveState = ReadRunState(lastVersion, lastState);
		if (haveState && lastVersion == runningVersion && lastState == "probing")
		{
			breakerTripped = true;
			safeMode = true;
			safeModeReason = "the server stopped (crash or freeze) the last time hns-movement ran on this CS2 version, before a player had "
							 "played long enough to confirm it works";
			snprintf(reason, maxlen, "%s", safeModeReason.c_str());
			return false;
		}
		if (haveState && lastVersion == runningVersion && lastState == "good")
		{
			probing = false;
			return true;
		}

		// New CS2 version (or no state file yet, or the last run ended cleanly before it was confirmed).
		probing = true;
		probingWritten = false;
		confirmedSeconds = 0.0;
		HNS_LOG_INFO(LogChannel::General,
					 "CS2 server version %u not confirmed yet: it is once a player has been in the game for %.0f seconds.\n", runningVersion,
					 settings.safetyConfirmSeconds);
		return true;
	}

	bool IsSafeMode()
	{
		return safeMode;
	}

	bool RunSelfTest(char *reason, size_t maxlen)
	{
		reason[0] = '\0';
		if (selfTestPassed)
		{
			return true;
		}
		selfTestRan = true;
		selfTestPassed = SelfTest(reason, maxlen);
		if (selfTestPassed)
		{
			HNS_LOG_INFO(LogChannel::General, "Self-test passed.\n");
		}
		else
		{
			HNS_LOG_WARN(LogChannel::General, "Self-test FAILED: %s\n", reason);
		}
		if (!selfTestPassed)
		{
			safeMode = true;
			safeModeReason = std::string("self-test failed: ") + reason;
			// Not this build's fault to probe further; leave the breaker as it is.
			probing = false;
		}
		return selfTestPassed;
	}

	void OnGameFrame()
	{
		if (!probing)
		{
			return;
		}
		f64 now = Plat_FloatTime();
		f64 delta = lastFrameTime > 0.0 ? now - lastFrameTime : 0.0;
		lastFrameTime = now;
		// Ignore hitches and hibernation gaps; they say nothing about the plugin.
		if (delta <= 0.0 || delta > 1.0)
		{
			return;
		}
		if (g_pKZUtils->GetPlayerCount() == 0)
		{
			return;
		}
		if (!probingWritten)
		{
			// From here until it is confirmed, a server that dies is blamed on the plugin.
			WriteRunState(runningVersion, "probing");
			probingWritten = true;
		}
		confirmedSeconds += delta;
		if (confirmedSeconds >= settings.safetyConfirmSeconds)
		{
			probing = false;
			WriteRunState(runningVersion, "good");
			HNS_LOG_INFO(LogChannel::General, "CS2 server version %u confirmed working.\n", runningVersion);
		}
	}

	void OnUnload()
	{
		// A clean shutdown is not a crash: let the next start probe this version again instead of starting in safe mode.
		if (probing && probingWritten)
		{
			WriteRunState(runningVersion, "unconfirmed");
		}
	}

	void PrintSafeModeWarning()
	{
		if (!safeMode)
		{
			return;
		}
		HNS_LOG_WARN(LogChannel::General, "======================================================================\n");
		HNS_LOG_WARN(LogChannel::General, "SAFE MODE: hns-movement is loaded but switched OFF (no hooks, nothing changed).\n");
		HNS_LOG_WARN(LogChannel::General, "Reason: %s.\n", safeModeReason.c_str());
		HNS_LOG_WARN(LogChannel::General, "CS2 server version %u. A CS2 update probably needs a new hns-movement build.\n", runningVersion);
		HNS_LOG_WARN(LogChannel::General, "After installing a fixed build: hns_safety_reset in the server console, then restart.\n");
		HNS_LOG_WARN(LogChannel::General, "======================================================================\n");
	}

	void PrintStatus()
	{
		META_CONPRINTF("---- hns-movement safety ----\n");
		META_CONPRINTF("CS2 server version %u\n", runningVersion);
		const char *test = selfTestPassed ? "passed" : !selfTestRan ? "not run (runs at map start, skipped in safe mode)" : "FAILED";
		META_CONPRINTF("self-test   %s\n", test);
		const char *breaker = !settings.safety ? "off (hns_safety 0)"
							  : breakerTripped ? "TRIPPED (the server died last time on this version before it was confirmed)"
							  : probing        ? "probing (not confirmed yet)"
											   : "confirmed";
		META_CONPRINTF("breaker     %s\n", breaker);
		if (probing)
		{
			META_CONPRINTF("confirmed   %.0f / %.0f s with a player in the game\n", confirmedSeconds, settings.safetyConfirmSeconds);
		}
		META_CONPRINTF("mode        %s\n", safeMode ? "SAFE MODE (switched off)" : "normal");
		if (safeMode)
		{
			META_CONPRINTF("reason      %s\n", safeModeReason.c_str());
		}
	}
} // namespace hns::safety

CON_COMMAND_F(hns_safety_status, "Show the hns-movement CS2-update safety state.", FCVAR_NONE)
{
	if (context.GetPlayerSlot().Get() != -1)
	{
		return;
	}
	hns::safety::PrintStatus();
}

CON_COMMAND_F(hns_safety_reset, "Clear the hns-movement crash breaker (after installing a fixed build). Takes effect on restart.", FCVAR_NONE)
{
	if (context.GetPlayerSlot().Get() != -1)
	{
		return;
	}
	char path[1024];
	V_snprintf(path, sizeof(path), "%s/csgo/%s", Plat_GetGameDirectory(), HNS_RUNSTATE_PATH);
	remove(path);
	META_CONPRINTF("Crash breaker cleared. Restart the server to load hns-movement normally.\n");
}
