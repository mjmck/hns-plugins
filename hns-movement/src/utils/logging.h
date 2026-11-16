#pragma once
#include "tier0/dbg.h"

// Minimal logging for hns-movement. CS2KZ's channel/file logging was dropped; everything goes to the server console.
// The channel argument is kept so call sites read the same as upstream and can be routed later if needed.
enum class LogChannel
{
	General,
	Movement,
	Player,
	Cvar,
};

#define HNS_LOG_PREFIX "[HNS-Movement] "

#define HNS_LOG_INFO(channel, fmt, ...)  Msg(HNS_LOG_PREFIX fmt, ##__VA_ARGS__)
#define HNS_LOG_DEBUG(channel, fmt, ...) DevMsg(HNS_LOG_PREFIX fmt, ##__VA_ARGS__)
#define HNS_LOG_WARN(channel, fmt, ...)  Warning(HNS_LOG_PREFIX fmt, ##__VA_ARGS__)
#define HNS_LOG_ERROR(channel, fmt, ...) Warning(HNS_LOG_PREFIX "ERROR: " fmt, ##__VA_ARGS__)

// Shared CS2KZ code logs with KZ_LOG_*; route it through the HNS logger.
#define KZ_LOG_INFO  HNS_LOG_INFO
#define KZ_LOG_DEBUG HNS_LOG_DEBUG
#define KZ_LOG_WARN  HNS_LOG_WARN
#define KZ_LOG_ERROR HNS_LOG_ERROR
