<div align="center">
  <img width="50" height="50" alt="cssharp" src="https://github.com/user-attachments/assets/3393573f-29be-46e1-bc30-fafaec573456" />
	<h3><strong>Redie</strong></h3>
	<h4>a plugin that allows players to go in Redie/Ghost mode</h4>
	<h2>
		<img src="https://img.shields.io/github/downloads/exkludera-cssharp/redie/total" alt="Downloads">
		<img src="https://img.shields.io/github/stars/exkludera-cssharp/redie?style=flat&logo=github" alt="Stars">
		<img src="https://img.shields.io/github/forks/exkludera-cssharp/redie?style=flat&logo=github" alt="Forks">
		<img src="https://img.shields.io/github/license/exkludera-cssharp/redie" alt="License">
	</h2>
	<!--<a href="https://discord.gg" target="_blank"><img src="https://img.shields.io/badge/Discord%20Server-7289da?style=for-the-badge&logo=discord&logoColor=white" /></a> <br>-->
	<a href="https://ko-fi.com/exkludera" target="_blank"><img src="https://img.shields.io/badge/KoFi-af00bf?style=for-the-badge&logo=kofi&logoColor=white" alt="Buy Me a Coffee at ko-fi.com" /></a>
	<a href="https://paypal.com/donate/?hosted_button_id=6AWPNVF5TLUC8" target="_blank"><img src="https://img.shields.io/badge/PayPal-0095ff?style=for-the-badge&logo=paypal&logoColor=white" alt="PayPal"  /></a>
	<a href="https://github.com/sponsors/exkludera" target="_blank"><img src="https://img.shields.io/badge/Sponsor-696969?style=for-the-badge&logo=github&logoColor=white" alt="GitHub Sponsor" /></a>
</div>

> inspired by [[CS:GO][Surf] Redie by Pyro_](https://forums.alliedmods.net/showthread.php?p=2198357)

<img src="https://github.com/user-attachments/assets/53e486cc-8da4-45ab-bc6e-eb38145aba36" height="200px"> <br>

## Requirements
- [MetaMod](https://github.com/alliedmodders/metamod-source)
- [CounterStrikeSharp](https://github.com/roflmuffin/CounterStrikeSharp)

## Config
<details>
<summary>Redie.json</summary>
  
```json
{
  "Prefix": "{purple}[Redie]{grey}",
  "Commands": ["css_redie", "css_ghost"],
  "Messages": true,
  "Message_Redie": "You have been respawned as a ghost",
  "Message_UnRedie": "You are no longer a ghost",
  "Message_RoundOver": "You can't become a ghost after the round has ended"
}
```
</details>

## hns-plugins fork
Changes from upstream (Next-il/redie v0.0.2):
- Builds against CounterStrikeSharp 1.0.376 (.NET 10). Damage blocking uses `Listeners.OnPlayerTakeDamagePre`, which replaced the `CBaseEntity_TakeDamageOldFunc` hook.
- Ghosts can no longer pick up items (the pickup hook returned `Allowed` by mistake).
- Ghosts are invincible. Upstream only blocked player and fall damage and took the player out of ghost mode on any other damage.
- Becoming a ghost is not allowed between round end and the next round start (`Message_RoundOver`). Leaving ghost mode still works.
- Players who were ghosts are visible again on their next spawn. CS2 reuses the pawn, so upstream left them transparent, shadowless and no-block in the next round.

### Build
Needs the .NET 10 SDK.
```
dotnet build redie/src/Redie.csproj -c Release
```

### Install
Copy `redie/src/bin/Release/net10.0/Redie.dll` to `addons/counterstrikesharp/plugins/Redie/` on the server. Do not copy `CounterStrikeSharp.API.dll` or the other dependency DLLs from the build folder; the server already has them.
