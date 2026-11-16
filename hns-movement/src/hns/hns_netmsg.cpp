#include "hns_netmsg.h"
#include "hns_safety.h"
#include "utils/utils.h"

#include "networksystem/inetworkmessages.h"
#include "tier0/platform.h"

#include <cstring>

#include "tier0/memdbgon.h"

namespace hns::netmsg
{
	struct Definition
	{
		const char *label;
		// Registered name, the protobuf class name.
		const char *fullName;
		// Substring of the registered name, as CS2KZ looks it up.
		const char *partialName;
		// Protobuf message id: NET_Messages (common/networkbasetypes.proto), EBaseUserMessages
		// (game/shared/usermessages.proto), EBaseGameEvents (game/shared/gameevents.proto).
		i32 id;
	};

	static_global const Definition definitions[COUNT] = {
		{"SetConVar", "CNETMsg_SetConVar", "SetConVar", 6},
		{"TextMsg", "CUserMessageTextMsg", "TextMsg", 124},
		{"SayText2", "CUserMessageSayText2", "SayText2", 118},
		{"SosStartSoundEvent", "CMsgSosStartSoundEvent", "SosStartSoundEvent", 208},
	};

	enum class State
	{
		Unresolved,
		Found,
		Missing,
		BadLayout,
	};

	struct Entry
	{
		State state = State::Unresolved;
		NetMessageInfo_t *info = nullptr;
		const char *foundBy = "";
		char nameRead[64] = {};
		char problem[160] = {};
		f64 lastAttempt = -1000.0;
		bool warned = false;
	};

	static_global Entry entries[COUNT];

	// Missing messages are looked up again at most this often, so a send every tick does not hammer the engine.
	static constexpr f64 RETRY_INTERVAL = 5.0;

	// NetMessageInfo_t starts with CUtlString m_szName (one char pointer) then NetMessageAllocateFn m_pfnAllocateMessage.
	// Read as raw pointers: a NetMessageInfo_t copy would free the engine's strings when destroyed.
	static constexpr size_t NAME_OFFSET = 0;
	static constexpr size_t ALLOCATOR_OFFSET = sizeof(void *);
	static_assert(sizeof(CUtlString) == sizeof(void *), "CUtlString layout changed: update NAME_OFFSET/ALLOCATOR_OFFSET");

	// Checks that a record returned by the engine looks like NetMessageInfo_t for this message, with reads that
	// cannot crash. Fills problem (and returns false) if not.
	static_function bool Validate(NetMessageInfo_t *info, const Definition &def, Entry &entry)
	{
		u8 raw[2 * sizeof(void *)];
		if (!hns::safety::SafeRead(info, raw, sizeof(raw)))
		{
			snprintf(entry.problem, sizeof(entry.problem), "record not readable");
			return false;
		}
		const char *namePtr = nullptr;
		void *allocator = nullptr;
		memcpy(&namePtr, raw + NAME_OFFSET, sizeof(namePtr));
		memcpy(&allocator, raw + ALLOCATOR_OFFSET, sizeof(allocator));

		char name[64] = {};
		// The string may end close to the end of a page: try a long read, then just long enough for the expected name.
		if (!hns::safety::SafeRead(namePtr, name, sizeof(name) - 1))
		{
			memset(name, 0, sizeof(name));
			size_t len = strlen(def.fullName) + 1;
			if (!hns::safety::SafeRead(namePtr, name, len < sizeof(name) - 1 ? len : sizeof(name) - 1))
			{
				snprintf(entry.problem, sizeof(entry.problem), "name not readable (struct layout changed?)");
				return false;
			}
		}
		name[sizeof(name) - 1] = '\0';
		V_strncpy(entry.nameRead, name, sizeof(entry.nameRead));
		if (!strstr(name, def.partialName))
		{
			snprintf(entry.problem, sizeof(entry.problem), "name reads as '%.40s' (struct layout changed?)", name);
			return false;
		}
		u8 code;
		if (!allocator || !hns::safety::SafeRead(allocator, &code, 1))
		{
			snprintf(entry.problem, sizeof(entry.problem), "allocator not readable (struct layout changed?)");
			return false;
		}
		return true;
	}

	static_function void Resolve(Message message, bool force)
	{
		Entry &entry = entries[message];
		const Definition &def = definitions[message];
		if (entry.state == State::Found)
		{
			return;
		}
		f64 now = Plat_FloatTime();
		if (!force && now - entry.lastAttempt < RETRY_INTERVAL)
		{
			return;
		}
		entry.lastAttempt = now;
		if (!g_pNetworkMessages)
		{
			entry.state = State::Missing;
			snprintf(entry.problem, sizeof(entry.problem), "network message system not available");
			return;
		}

		bool sawBadRecord = false;
		char badProblem[160] = {};
		auto tryCandidate = [&](NetMessageInfo_t *candidate, const char *how) -> bool
		{
			if (!candidate)
			{
				return false;
			}
			if (Validate(candidate, def, entry))
			{
				entry.state = State::Found;
				entry.info = candidate;
				entry.foundBy = how;
				entry.problem[0] = '\0';
				return true;
			}
			if (!sawBadRecord)
			{
				sawBadRecord = true;
				V_strncpy(badProblem, entry.problem, sizeof(badProblem));
			}
			return false;
		};

		if (tryCandidate(g_pNetworkMessages->FindNetworkMessage(def.fullName), "full name"))
		{
			return;
		}
		if (tryCandidate(g_pNetworkMessages->FindNetworkMessagePartial(def.partialName), "partial name"))
		{
			return;
		}
		// Last resort: CS2KZ never calls this one, so it is only reached when both name lookups found nothing usable.
		if (tryCandidate(g_pNetworkMessages->FindNetworkMessageById(def.id), "id"))
		{
			return;
		}

		if (sawBadRecord)
		{
			entry.state = State::BadLayout;
			V_strncpy(entry.problem, badProblem, sizeof(entry.problem));
		}
		else
		{
			entry.state = State::Missing;
			snprintf(entry.problem, sizeof(entry.problem), "not found by name or id %d", def.id);
		}
	}

	NetMessageInfo_t *Get(Message message)
	{
		Entry &entry = entries[message];
		if (entry.state == State::Found)
		{
			return entry.info;
		}
		if (entry.state == State::BadLayout)
		{
			return nullptr;
		}
		Resolve(message, false);
		if (entry.state == State::Found)
		{
			return entry.info;
		}
		if (!entry.warned)
		{
			entry.warned = true;
			HNS_LOG_WARN(LogChannel::General, "Network message %s: %s. Skipping sends that need it (no crash).\n", definitions[message].label,
						 entry.problem);
		}
		return nullptr;
	}

	void ResolveAll()
	{
		for (i32 i = 0; i < COUNT; i++)
		{
			Resolve((Message)i, true);
		}
	}

	bool LayoutBroken(Message message, char *reason, size_t maxlen)
	{
		const Entry &entry = entries[message];
		if (entry.state != State::BadLayout)
		{
			return false;
		}
		snprintf(reason, maxlen, "network message %s: %s", definitions[message].label, entry.problem);
		return true;
	}

	void PrintSummary()
	{
		for (i32 i = 0; i < COUNT; i++)
		{
			const Entry &entry = entries[i];
			const Definition &def = definitions[i];
			switch (entry.state)
			{
				case State::Found:
					HNS_LOG_INFO(LogChannel::General, "Network message %s: ok (found by %s as %s)\n", def.label, entry.foundBy, entry.nameRead);
					break;
				case State::Unresolved:
					HNS_LOG_INFO(LogChannel::General, "Network message %s: not looked up yet (happens at map start)\n", def.label);
					break;
				default:
					HNS_LOG_WARN(LogChannel::General, "Network message %s: PROBLEM - %s\n", def.label, entry.problem);
					break;
			}
		}
	}
} // namespace hns::netmsg

CON_COMMAND_F(hns_netmsg_status, "Show whether hns-movement found the network messages it sends (cvars, text, chat, sounds).", FCVAR_NONE)
{
	hns::netmsg::PrintSummary();
}
