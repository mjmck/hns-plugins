#pragma once

#include <string>
#include <string_view>

#include "common.h"
#include "version_gen.h"

class HNSPlugin;
extern HNSPlugin g_HNSPlugin;

// Metamod entry point for hns-movement.
// Owns the movement physics for a Hide and Seek server: KZ-style bhop/ladder/surf fixes ported from CS2KZ's Classic mode,
// plus enforcement of the movement convars. Gameplay (freeze, knife, team switching) stays in ChaseMod.
class HNSPlugin : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late);
	bool Unload(char *error, size_t maxlen);
	bool Pause(char *error, size_t maxlen);
	bool Unpause(char *error, size_t maxlen);
	void AllPluginsLoaded();
	void OnPluginLoad(PluginId id);
	void OnPluginUnload(PluginId id);

public:
	const char *GetAuthor()
	{
		return PLUGIN_AUTHOR;
	}

	const char *GetName()
	{
		return PLUGIN_DISPLAY_NAME;
	}

	const char *GetDescription()
	{
		return PLUGIN_DESCRIPTION;
	}

	const char *GetURL()
	{
		return PLUGIN_URL;
	}

	const char *GetLicense()
	{
		return PLUGIN_LICENSE;
	}

	const char *GetVersion()
	{
		return PLUGIN_FULL_VERSION;
	}

	const char *GetDate()
	{
		return __DATE__;
	}

	const char *GetLogTag()
	{
		return PLUGIN_LOGTAG;
	}

	virtual void OnLevelInit(char const *pMapName, char const *pMapEntities, char const *pOldLevel, char const *pLandmarkName, bool loadGame,
							 bool background) override;

	// Set while the engine is inside CCSPlayerController::PhysicsSimulate, so KZUtils::GetServerGlobals returns the
	// snapshot taken at the start of the frame instead of the per-player command globals.
	bool simulatingPhysics = false;
	CGlobalVars serverGlobals;
	bool unloading = false;
	bool loading = true;

private:
	std::string m_sCurrentMap;

public:
	std::string_view GetCurrentMap()
	{
		return m_sCurrentMap;
	}
};
