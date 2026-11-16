#include "hns_plugin.h"

#include "entity2/entitysystem.h"
#include "steam/steam_gameserver.h"

#include "utils/utils.h"
#include "utils/addresses.h"
#include "utils/hooks.h"
#include "utils/gameconfig.h"
#include "utils/ctimer.h"

#include "hns/hns_player.h"
#include "hns/hns_movement.h"
#include "hns/hns_cvars.h"
#include "hns/hns_config.h"
#include "hns/hns_commands.h"
#include "hns/hns_gameplay.h"
#include "hns/hns_compat.h"

#include "tier0/memdbgon.h"

HNSPlugin g_HNSPlugin;

PLUGIN_EXPOSE(HNSPlugin, g_HNSPlugin);

bool HNSPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	modules::Initialize();

	if (!interfaces::Initialize(ismm, error, maxlen))
	{
		return false;
	}

	META_CONVAR_REGISTER(FCVAR_NONE);

	if (!utils::Initialize(ismm, error, maxlen))
	{
		return false;
	}

	// Every movement cvar must exist before anything is hooked. A missing one means the game changed under us.
	if (!hns::cvars::Init(error, maxlen))
	{
		return false;
	}

	if (!hooks::Initialize(error, maxlen))
	{
		return false;
	}

	ismm->AddListener(this, this);

	hns::config::Load();
	hns::cvars::DisableReplication();
	hns::cvars::ApplyAll();
	hns::commands::Init();
	hns::gameplay::ApplyServerSettings();
	hns::compat::Check();

	if (late)
	{
		g_pHNSPlayerManager->ResetPlayers();
		hns::cvars::SendToAllClients();
	}

	HNS_LOG_INFO(LogChannel::General, "Loaded %s (late load: %s)\n", PLUGIN_FULL_VERSION, late ? "true" : "false");
	loading = false;
	return true;
}

bool HNSPlugin::Unload(char *error, size_t maxlen)
{
	this->unloading = true;
	hns::cvars::EnableReplication();
	for (i32 i = 0; i < MAX_PLAYERS; i++)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(i));
		if (player && player->movement)
		{
			player->movement->Cleanup();
		}
	}
	g_pHNSPlayerManager->Cleanup();
	hooks::Cleanup();
	RemoveAllTimers();
	hns::cvars::Cleanup();
	utils::Cleanup();
	ConVar_Unregister();
	return true;
}

void HNSPlugin::AllPluginsLoaded()
{
	g_pHNSPlayerManager->ResetPlayers();
}

void HNSPlugin::OnLevelInit(char const *pMapName, char const *pMapEntities, char const *pOldLevel, char const *pLandmarkName, bool loadGame,
							bool background)
{
	m_sCurrentMap = pMapName;
	hns::gameplay::OnMapStart();
}

bool HNSPlugin::Pause(char *error, size_t maxlen)
{
	return true;
}

bool HNSPlugin::Unpause(char *error, size_t maxlen)
{
	return true;
}

