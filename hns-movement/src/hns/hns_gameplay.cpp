#include "hns_gameplay.h"
#include "hns_player.h"
#include "hns_config.h"
#include "hns_weapons.h"
#include "hns_eventsounds.h"
#include "utils/utils.h"
#include "sdk/gamerules.h"
#include "sdk/services.h"
#include "sdk/entity/cbaseplayerweapon.h"
#include "igameevents.h"
#include "utils/interfaces.h"
#include "convar.h"

#include "tier0/memdbgon.h"

using hns::config::settings;

namespace hns::gameplay
{
	// Server time when the CT freeze ends. 0 = no freeze active.
	static_global f32 freezeEndTime {};
	static_global i32 lastCountdownShown = -1;

	// sv_falldamage_scale before the freeze turned it off. < 0 = not overridden.
	static_global f32 savedFallDamageScale = -1.0f;
	// Which slots currently show the frozen tint/effect.
	static_global bool frozenVisual[MAX_PLAYERS + 1] {};

	static_global bool shouldSwitchTeams {};
	static_global i32 terroristWinStreak {};

	bool IsFrozen(HNSPlayer *player);
	void ApplyServerSettings();

	static_function f32 Now()
	{
		return g_pKZUtils->GetServerGlobals()->curtime;
	}

	static_function bool IsWarmup()
	{
		CCSGameRules *rules = g_pKZUtils->GetGameRules();
		return rules && rules->m_bWarmupPeriod();
	}

	static_function void DisableFallDamage()
	{
		if (!settings.freezeNoFallDamage || savedFallDamageScale >= 0.0f)
		{
			return;
		}
		CConVarRef<float> scale("sv_falldamage_scale");
		if (!scale.IsValidRef() || !scale.IsConVarDataAvailable())
		{
			return;
		}
		savedFallDamageScale = scale.Get();
		// Through the console, like ChaseMod, so the value replicates to clients.
		interfaces::pEngine->ServerCommand("sv_falldamage_scale 0");
	}

	static_function void RestoreFallDamage()
	{
		if (savedFallDamageScale < 0.0f)
		{
			return;
		}
		char cmd[64];
		V_snprintf(cmd, sizeof(cmd), "sv_falldamage_scale %g", savedFallDamageScale);
		interfaces::pEngine->ServerCommand(cmd);
		savedFallDamageScale = -1.0f;
	}

	static_function void SetFrozenVisual(HNSPlayer *player, bool frozen)
	{
		CCSPlayerPawn *pawn = player->GetPlayerPawn();
		if (!pawn)
		{
			return;
		}
		Color color = pawn->m_clrRender();
		if (frozen)
		{
			pawn->m_clrRender(Color(4, 58, 140, color.a()));
			pawn->m_flHealthShotBoostExpirationTime(freezeEndTime);
		}
		else
		{
			pawn->m_clrRender(Color(255, 255, 255, color.a()));
			pawn->m_flHealthShotBoostExpirationTime(Now());
		}
	}

	// Brings each player's tint/effect in line with IsFrozen. Cheap: only touches players whose state changed.
	static_function void UpdateFrozenVisuals()
	{
		for (i32 i = 0; i < MAX_PLAYERS; i++)
		{
			HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(i));
			if (!player || !player->IsConnectedController())
			{
				frozenVisual[i] = false;
				continue;
			}
			bool want = settings.freezeEffects && IsFrozen(player);
			if (want != frozenVisual[i])
			{
				frozenVisual[i] = want;
				SetFrozenVisual(player, want);
			}
		}
	}

	static_function void EndCTFreeze()
	{
		freezeEndTime = 0.0f;
		RestoreFallDamage();
		UpdateFrozenVisuals();
	}

	static_function void StartCTFreeze()
	{
		if (settings.ctFreezeTime <= 0.0f || IsWarmup())
		{
			freezeEndTime = 0.0f;
			return;
		}
		freezeEndTime = Now() + settings.ctFreezeTime;
		lastCountdownShown = -1;
		DisableFallDamage();
		UpdateFrozenVisuals();
	}

	static_function void SwitchTeams()
	{
		for (i32 i = 0; i < MAX_PLAYERS; i++)
		{
			HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(i));
			if (!player || !player->IsConnectedController())
			{
				continue;
			}
			CCSPlayerController *controller = player->GetController();
			if (!controller)
			{
				continue;
			}
			i32 team = controller->m_iTeamNum();
			if (team == CS_TEAM_CT)
			{
				controller->SwitchTeam(CS_TEAM_T);
			}
			else if (team == CS_TEAM_T)
			{
				controller->SwitchTeam(CS_TEAM_CT);
			}
		}
	}

	void OnFireEvent(IGameEvent *event)
	{
		hns::eventsounds::OnFireEvent(event);
		const char *name = event->GetName();
		if (KZ_STREQI(name, "round_end"))
		{
			if (!settings.teamSwitch || IsWarmup())
			{
				return;
			}
			i32 winner = event->GetInt("winner");
			shouldSwitchTeams = false;
			if (winner == CS_TEAM_CT)
			{
				terroristWinStreak = 0;
				shouldSwitchTeams = true;
				utils::CPrintChatAll(" {blue}CT{grey} win - teams are being switched.");
			}
			else if (winner == CS_TEAM_T)
			{
				terroristWinStreak++;
				if (settings.tWinStreakSwitch > 0 && terroristWinStreak >= settings.tWinStreakSwitch)
				{
					utils::CPrintChatAll(" {gold}T{grey} win - teams are being switched ({default}%i{grey} wins in a row).", terroristWinStreak);
					terroristWinStreak = 0;
					shouldSwitchTeams = true;
				}
				else
				{
					utils::CPrintChatAll(" {gold}T{grey} win.");
				}
			}
		}
		else if (KZ_STREQI(name, "player_team"))
		{
			// The client prints the join message from this event unless it is flagged silent.
			if (settings.silentTeamChange)
			{
				event->SetBool("silent", true);
			}
		}
		else if (KZ_STREQI(name, "player_spawn"))
		{
			// userid is the controller's entity index - 1 (player slot) for player_spawn in CS2.
			CEntityInstance *controller = event->GetPlayerController("userid");
			if (controller)
			{
				hns::weapons::OnPlayerSpawn(controller->m_pEntity->m_EHandle.GetEntryIndex() - 1);
			}
		}
		else if (KZ_STREQI(name, "round_prestart"))
		{
			// Again every round: game mode cfgs executed after map start can turn mp_autokick back on.
			ApplyServerSettings();
			hns::weapons::ResetStabCooldowns();
			EndCTFreeze();
			if (shouldSwitchTeams)
			{
				shouldSwitchTeams = false;
				SwitchTeams();
			}
		}
		else if (KZ_STREQI(name, "round_freeze_end"))
		{
			// With mp_freezetime 0 this fires right as the round starts.
			StartCTFreeze();
		}
	}

	void ApplyServerSettings()
	{
		if (settings.disableAutokick)
		{
			interfaces::pEngine->ServerCommand("mp_autokick 0");
		}
		if (settings.disableTeamIntro)
		{
			interfaces::pEngine->ServerCommand("mp_team_intro_time 0");
		}
	}

	void OnMapStart()
	{
		ApplyServerSettings();
		freezeEndTime = 0.0f;
		RestoreFallDamage();
		for (bool &v : frozenVisual)
		{
			v = false;
		}
		shouldSwitchTeams = false;
		terroristWinStreak = 0;
	}

	void OnGameFrame()
	{
		if (freezeEndTime <= 0.0f)
		{
			return;
		}
		f32 left = freezeEndTime - Now();
		if (left <= 0.0f)
		{
			EndCTFreeze();
			utils::PrintCentreAll("Seekers released!");
			return;
		}
		// Covers players who spawn or switch team mid-freeze.
		UpdateFrozenVisuals();
		i32 shown = (i32)ceilf(left);
		if (shown != lastCountdownShown)
		{
			lastCountdownShown = shown;
			utils::PrintCentreAll("Seekers released in %i...", shown);
			hns::eventsounds::OnFreezeCountdown(shown);
		}
	}

	bool IsFrozen(HNSPlayer *player)
	{
		if (freezeEndTime <= 0.0f || Now() >= freezeEndTime)
		{
			return false;
		}
		CCSPlayerController *controller = player->GetController();
		return controller && controller->m_iTeamNum() == CS_TEAM_CT && player->IsAlive();
	}

	void ApplyFreezeToMovement(HNSPlayer *player)
	{
		if (!IsFrozen(player) || !player->currentMoveData)
		{
			return;
		}
		CMoveData *mv = player->currentMoveData;
		mv->m_flForwardMove = 0.0f;
		mv->m_flSideMove = 0.0f;
		mv->m_flUpMove = 0.0f;
		mv->m_vecVelocity.x = 0.0f;
		mv->m_vecVelocity.y = 0.0f;
		CCSPlayer_MovementServices *ms = player->GetMoveServices();
		if (ms)
		{
			// Held and newly-pressed states: no jumping while frozen.
			ms->m_nButtons().m_pButtonStates[0] &= ~IN_JUMP;
			ms->m_nButtons().m_pButtonStates[1] &= ~IN_JUMP;
		}
	}

	void ApplyFreezeToWeapons(HNSPlayer *player)
	{
		if (!IsFrozen(player))
		{
			return;
		}
		CCSPlayerPawn *pawn = player->GetPlayerPawn();
		if (!pawn || !pawn->m_pWeaponServices())
		{
			return;
		}
		i32 unfreezeTick = g_pKZUtils->GetServerGlobals()->tickcount + (i32)((freezeEndTime - Now()) * ENGINE_FIXED_TICK_RATE) + 1;
		CUtlVector<CHandle<CBasePlayerWeapon>> *weapons = pawn->m_pWeaponServices()->m_hMyWeapons();
		FOR_EACH_VEC(*weapons, i)
		{
			CBasePlayerWeapon *weapon = (*weapons)[i].Get();
			if (!weapon)
			{
				continue;
			}
			if (weapon->m_nNextPrimaryAttackTick() < unfreezeTick)
			{
				weapon->m_nNextPrimaryAttackTick(unfreezeTick);
			}
			if (weapon->m_nNextSecondaryAttackTick() < unfreezeTick)
			{
				weapon->m_nNextSecondaryAttackTick(unfreezeTick);
			}
		}
	}
} // namespace hns::gameplay
