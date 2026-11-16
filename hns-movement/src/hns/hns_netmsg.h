#pragma once
#include "common.h"

struct NetMessageInfo_t;

// The network messages hns-movement sends: cvar replication (movement settings), centre/console text, chat, sounds.
//
// Every send goes through Get(), never straight to g_pNetworkMessages, so that:
// - A lookup that finds nothing skips the send (with one warning) instead of crashing the server on the next line.
// - A message is looked up by full name, then partial name, then by its protobuf id, so one lookup style breaking in a
//   CS2 update does not take the feature down.
// - Once found, the record is cached: the engine keeps its message records for the life of the process.
//
// Checks only ever copy the engine's record into plain bytes. 16 Nov 2026: the first self-test copied it into a
// NetMessageInfo_t, whose CUtlString destructor then freed the engine's own message names; every later lookup by name
// failed and the server crashed on the first player join.
namespace hns::netmsg
{
	enum Message
	{
		SetConVar,
		TextMsg,
		SayText2,
		SosStartSoundEvent,
		COUNT
	};

	// The message's record, or nullptr if it cannot be found (the caller must then skip the send).
	NetMessageInfo_t *Get(Message message);

	// Looks every message up now (map start). Missing messages are retried later by Get().
	void ResolveAll();

	// True if a lookup returned a record that does not look like NetMessageInfo_t (engine layout changed). Every send
	// would call garbage, so the self-test switches the plugin off. A message that is simply missing returns false.
	bool LayoutBroken(Message message, char *reason, size_t maxlen);

	// One line per message: found or not, and how. Printed at map start and by hns_netmsg_status.
	void PrintSummary();
} // namespace hns::netmsg
