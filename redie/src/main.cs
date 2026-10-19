using CounterStrikeSharp.API;
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Modules.Utils;
using CounterStrikeSharp.API.Modules.Memory;
using CounterStrikeSharp.API.Core.Translations;
using CounterStrikeSharp.API.Modules.Memory.DynamicFunctions;
using CounterStrikeSharp.API.Modules.Entities.Constants;
using CounterStrikeSharp.API.Modules.UserMessages;
using System.Drawing;

public class Plugin : BasePlugin, IPluginConfig<Config>
{
    public override string ModuleName => "Redie";
    public override string ModuleVersion => "1.0.2";
    public override string ModuleAuthor => "exkludera";

    public HashSet<CCSPlayerController> RediePlayers = new();

    // Set from round_end until the next round_start. New ghosts are not allowed then.
    bool roundOver;

    // How a player's pawn looked before they became a ghost, by player slot. CS2 reuses the same pawn when the player
    // respawns, so the ghost's transparency, missing shadow and no-block collision would otherwise carry over into the
    // next life. Restored on their next spawn.
    record struct PawnLook(Color Render, float Shadow, byte CollisionAttributeGroup, byte CollisionGroup);
    readonly Dictionary<int, PawnLook> savedLooks = new();

    public override void Load(bool hotReload)
    {
        foreach (var cmd in Config.Commands)
            AddCommand(cmd, "Redie Command", (player, command) => Command_Redie(player));

        RegisterEventHandler<EventRoundStart>(EventRoundStart, HookMode.Pre);
        RegisterEventHandler<EventRoundEnd>(EventRoundEnd);
        RegisterEventHandler<EventPlayerTeam>(EventPlayerTeam);
        RegisterEventHandler<EventPlayerDeath>(EventPlayerDeath, HookMode.Pre);
        RegisterEventHandler<EventPlayerSpawn>(EventPlayerSpawn);
        RegisterEventHandler<EventPlayerDisconnect>(EventPlayerDisconnect);

        RegisterListener<Listeners.CheckTransmit>(CheckTransmit);

        HookUserMessage(208, CMsgSosStartSoundEvent, HookMode.Pre);

        HookEntityOutput("*", "*", Disrupting, HookMode.Pre);

        VirtualFunctions.CCSPlayer_ItemServices_CanAcquireFunc.Hook(CanAcquireFunc, HookMode.Pre);
        RegisterListener<Listeners.OnPlayerTakeDamagePre>(OnPlayerTakeDamagePre);
    }

    public override void Unload(bool hotReload)
    {
        foreach (var cmd in Config.Commands)
            RemoveCommand(cmd, (player, command) => Command_Redie(player));

        DeregisterEventHandler<EventRoundStart>(EventRoundStart, HookMode.Pre);
        DeregisterEventHandler<EventRoundEnd>(EventRoundEnd);
        DeregisterEventHandler<EventPlayerTeam>(EventPlayerTeam);
        DeregisterEventHandler<EventPlayerDeath>(EventPlayerDeath, HookMode.Pre);
        DeregisterEventHandler<EventPlayerSpawn>(EventPlayerSpawn);
        DeregisterEventHandler<EventPlayerDisconnect>(EventPlayerDisconnect);

        RemoveListener<Listeners.CheckTransmit>(CheckTransmit);

        UnhookUserMessage(208, CMsgSosStartSoundEvent, HookMode.Pre);

        UnhookEntityOutput("*", "*", Disrupting, HookMode.Pre);

        VirtualFunctions.CCSPlayer_ItemServices_CanAcquireFunc.Unhook(CanAcquireFunc, HookMode.Pre);
        RemoveListener<Listeners.OnPlayerTakeDamagePre>(OnPlayerTakeDamagePre);
    }

    public Config Config { get; set; } = new Config();
    public void OnConfigParsed(Config config)
    {
        Config = config;
        Config.Prefix = StringExtensions.ReplaceColorTags(config.Prefix);
    }

    void Command_Redie(CCSPlayerController? player)
    {
        if (player == null || player.PawnIsAlive || player.Team == CsTeam.Spectator || player.Team == CsTeam.None)
            return;

        if (!RediePlayers.Contains(player))
        {
            // Leaving ghost mode still works after the round ends; becoming a ghost does not.
            if (roundOver)
            {
                if (Config.Messages)
                    player.PrintToChat($"{Config.Prefix} {Config.Message_RoundOver}");
                return;
            }
            Redie(player);
        }

        else UnRedie(player);
    }

    void Redie(CCSPlayerController player)
    {
        var playerPawn = player.PlayerPawn.Value;
        if (playerPawn == null) return;

        player.Respawn();
        player.RemoveWeapons();

        RediePlayers.Add(player);

        Server.NextFrame(() =>
        {
            if (!playerPawn.IsValid) return;

            if (!savedLooks.ContainsKey(player.Slot))
            {
                savedLooks[player.Slot] = new PawnLook(playerPawn.Render, playerPawn.ShadowStrength,
                    playerPawn.Collision.CollisionAttribute.CollisionGroup, playerPawn.Collision.CollisionGroup);
            }

            playerPawn.Collision.CollisionAttribute.CollisionGroup = (byte)CollisionGroup.COLLISION_GROUP_DISSOLVING; //noblock
            playerPawn.Collision.CollisionGroup = (byte)CollisionGroup.COLLISION_GROUP_DISSOLVING; //noblock
            playerPawn.ShadowStrength = 0f;
            playerPawn.Render = Color.Transparent; //for ragdoll if player unredie

            //timer to avoid blackscreen
            AddTimer(0.25f, () =>
            {
                playerPawn.LifeState = (byte)LifeState_t.LIFE_DYING;
            });
        });

        if (Config.Messages)
            player.PrintToChat($"{Config.Prefix} {Config.Message_Redie}");
    }

    void UnRedie(CCSPlayerController player)
    {
        var playerPawn = player.PlayerPawn.Value;
        if (playerPawn == null) return;

        Server.NextFrame(() =>
        {
            playerPawn.LifeState = (byte)LifeState_t.LIFE_ALIVE;
            player.CommitSuicide(false, true);
        });
    }

    HookResult EventRoundStart(EventRoundStart @event, GameEventInfo info)
    {
        RediePlayers.Clear();
        roundOver = false;

        return HookResult.Continue;
    }

    HookResult EventRoundEnd(EventRoundEnd @event, GameEventInfo info)
    {
        roundOver = true;

        return HookResult.Continue;
    }

    HookResult EventPlayerTeam(EventPlayerTeam @event, GameEventInfo info)
    {
        var player = @event.Userid;

        if (player == null || !player.IsValid)
            return HookResult.Continue;

        if (RediePlayers.Contains(player))
            RediePlayers.Remove(player);

        return HookResult.Continue;
    }

    HookResult EventPlayerDisconnect(EventPlayerDisconnect @event, GameEventInfo info)
    {
        var player = @event.Userid;
        if (player != null)
            savedLooks.Remove(player.Slot);

        return HookResult.Continue;
    }

    HookResult EventPlayerSpawn(EventPlayerSpawn @event, GameEventInfo info)
    {
        var player = @event.Userid;

        if (player == null || !player.IsValid || RediePlayers.Contains(player))
            return HookResult.Continue;

        if (!savedLooks.Remove(player.Slot, out var look))
            return HookResult.Continue;

        var pawn = player.PlayerPawn.Value;
        if (pawn == null || !pawn.IsValid)
            return HookResult.Continue;

        // Only undo what the ghost changed, so tints set by other plugins (freeze, stab) are left alone.
        if (pawn.Render.A == 0)
        {
            pawn.Render = look.Render.A == 0 ? Color.White : look.Render;
            Utilities.SetStateChanged(pawn, "CBaseModelEntity", "m_clrRender");
        }
        if (pawn.ShadowStrength == 0f)
        {
            pawn.ShadowStrength = look.Shadow;
            Utilities.SetStateChanged(pawn, "CBaseModelEntity", "m_flShadowStrength");
        }
        if (pawn.Collision.CollisionGroup == (byte)CollisionGroup.COLLISION_GROUP_DISSOLVING)
        {
            pawn.Collision.CollisionAttribute.CollisionGroup = look.CollisionAttributeGroup;
            pawn.Collision.CollisionGroup = look.CollisionGroup;
        }

        return HookResult.Continue;
    }

    HookResult EventPlayerDeath(EventPlayerDeath @event, GameEventInfo info)
    {
        var player = @event.Userid;

        if (player == null || !player.IsValid)
            return HookResult.Continue;

        if (RediePlayers.Contains(player))
        {
            info.DontBroadcast = true;
            RediePlayers.Remove(player);

            if (Config.Messages)
                player.PrintToChat($"{Config.Prefix} {Config.Message_UnRedie}");
        }

        return HookResult.Continue;
    }

    void CheckTransmit(CCheckTransmitInfoList infoList)
    {
        foreach ((CCheckTransmitInfo info, CCSPlayerController? player) in infoList)
        {
            if (player == null) continue;

            foreach (var hidden in RediePlayers)
            {
                if (player == hidden || player.Pawn.Value?.As<CCSPlayerPawnBase>().PlayerState == CSPlayerState.STATE_OBSERVER_MODE)
                    continue;

                var remove = hidden.Pawn.Value;
                if (remove == null) continue;

                info.TransmitEntities.Remove(remove);
            }
        }
    }

    HookResult CMsgSosStartSoundEvent(UserMessage um)
    {
        int entIndex = um.ReadInt("source_entity_index");
        var entHandle = NativeAPI.GetEntityFromIndex(entIndex);

        var pawn = new CBasePlayerPawn(entHandle);
        if (pawn == null || !pawn.IsValid || pawn.DesignerName != "player")
            return HookResult.Continue;

        var player = pawn.Controller?.Value?.As<CCSPlayerController>();
        if (player == null || !player.IsValid)
            return HookResult.Continue;

        if (RediePlayers.Contains(player))
        {
            foreach (var target in Utilities.GetPlayers())
            {
                if (target.IsBot) continue;
                if (target == player) continue;

                um.Recipients.Remove(target);
            }
        }

        return HookResult.Continue;
    }

    HookResult Disrupting(CEntityIOOutput output, string name, CEntityInstance activator, CEntityInstance caller, CVariant value, float delay)
    {
        if (activator.DesignerName != "player")
            return HookResult.Continue;

        var pawn = activator.As<CCSPlayerPawn>();
        if (pawn == null || !pawn.IsValid)
            return HookResult.Continue;

        var player = pawn.OriginalController?.Value?.As<CCSPlayerController>();
        if (player == null || player.IsBot)
            return HookResult.Continue;

        if (RediePlayers.Contains(player))
        {
            UnRedie(player);
            return HookResult.Handled;
        }

        return HookResult.Continue;
    }

    HookResult CanAcquireFunc(DynamicHook hook)
    {
        var player = hook.GetParam<CCSPlayer_ItemServices>(0).Pawn.Value.Controller.Value?.As<CCSPlayerController>();
        if (player == null) return HookResult.Continue;

        if (RediePlayers.Contains(player))
        {
            hook.SetReturn(AcquireResult.NotAllowedByProhibition);
            return HookResult.Handled;
        }

        return HookResult.Continue;
    }

    // CounterStrikeSharp 1.0.376 replaced the TakeDamageOld hook with this listener.
    // Returning Handled blocks the damage.
    HookResult OnPlayerTakeDamagePre(CCSPlayerPawn pawn, CTakeDamageInfo info)
    {
        if (pawn == null || !pawn.IsValid || info == null)
            return HookResult.Continue;

        var player = pawn.OriginalController?.Value?.As<CCSPlayerController>();
        if (player == null || player.IsBot) return HookResult.Continue;

        if (RediePlayers.Contains(player))
        {
            // The forced suicide that ends ghost mode must still go through.
            if (info.DamageFlags.HasFlag(TakeDamageFlags_t.DFLAG_FORCE_DEATH))
                return HookResult.Continue;

            // Ghosts are invincible: block all other damage (players, falls, world, map hazards).
            return HookResult.Handled;
        }

        return HookResult.Continue;
    }
}