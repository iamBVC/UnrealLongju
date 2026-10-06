# Player duels

The existing target-board Duel button now drives a server-authoritative,
map-local agreement rather than a placeholder message. No UI assets or legacy
source files are modified.

## Playing a duel

Select another player and click **Duel**. The challenger sees **Waiting...**;
the recipient sees **Accept duel** and a chat notification. The recipient selects
the challenger and accepts to enable mutual combat. While fighting, the button
is disabled and reads **Fighting**.

A lethal hit ends that fight normally, leaving the loser dead. The winner remains
agreed; after respawning, the loser can select the winner and click **Revenge** to
start a rematch with one acceptance. Ordinary respawn behaviour is unchanged.
Agreed duel deaths bypass the current aggressive-kill karma and equipment-drop
penalties. The legacy 15-second post-fight classification grace is retained for
revenge records, not applied to never-accepted challenges.

BANPK safezones always block hits, even during a duel. Agreements can be made in
a safezone, but players must leave it to fight. Members of the same party cannot
agree to or fight an agreed duel; joining the same party after acceptance also
blocks that duel permission. Other existing empire/aggressive/karma PvP rules
are unchanged by this feature.

## Ownership and cleanup

`UMT2DuelComponent` is a replicated default subobject of `AMT2PlayerState`. Each
participant has a view of the pair, replicated to relevant clients for UI and
targeting. No duel data is persisted to character saves. Multiple agreements
are supported, matching the legacy pair-based manager.

Requests use the caller's owned controller RPC, not a client-supplied challenger.
The server checks player identity, world, life state, party, request distance,
cooldown, capacity, and existing reciprocal agreement. Basic/skill combat refreshes
the server-only activity timestamps before damage executes, including killing
blows. A timer exists only while agreements are present; there is no component Tick.

Idle pairs expire on both sides. Logout, pawn/map teardown, and component teardown
remove reciprocal references. World transitions cancel agreements instead of
carrying stale actor identities between servers. `RequestDuel` and `CancelDuel`
are available on the player controller for further Blueprint UI integration;
the current target button uses the challenge/accept/revenge flow.

## Settings

Under `[/Script/Metin2.MT2GameplaySettings]` in `Config/DefaultGame.ini`
(Project Settings > Metin2 gameplay settings):

| Setting | Default | Purpose |
| --- | --- | --- |
| `DuelIdleTimeoutSeconds` | 600 | Legacy ten-minute inactivity lifetime |
| `DuelRequestRange` | 3000 cm | Same-map proximity check for requesting/accepting |
| `DuelRequestCooldownSeconds` | 1 | Server-side request throttling |
| `MaximumDuelAgreements` | 32 | Per-player bound on outstanding pairs |

Distance, throttling, capacity limits, and eager disconnect cleanup are deliberate
safety additions. The legacy `CPVPManager::Disconnect` was a no-op in this source
version; stale VID behaviour is not reproduced.

## Legacy references and scope

Compared the three local Graphify graphs, then inspected server
`game/src/pvp.cpp`, `pvp.h`, `cmd_general.cpp::do_pvp`, `battle.cpp::battle_is_attackable`,
and `char_battle.cpp::Dead`, plus client
`source/UserInterface/PythonNetworkStreamPhaseGame.cpp::RecvPVPPacket` and the
challenge/revenge tracking in `PythonPlayer.cpp`.

This ports player-versus-player challenge agreements, not the separate legacy
arena/tournament system. Complete legacy PK protection modes and unrelated death
experience penalties are not added here. Live two-client UI, network delivery,
late joining, and cooked dedicated-server validation must be reported separately
from native-world automation tests.

Validation (2026-10-06): Editor Win64 Development built; all 49 `Metin2` tests
passed in `Saved/Logs/DuelVerifiedRegressionTests.log`, including the two new native
duel fixtures, existing real PIE startup, and RHI area-preview regressions. The
duel fixtures check challenge/consent, revenge, safezones, range/cooldown, multiple
pairs, cancellation, activity/expiry, and a real lethal skill-to-death callback
with no agreed-duel karma penalty. This is not a remote-client duel/UI test or an
equipment-drop integration test. The RHI run used FXAA to isolate the existing
TSR ensure. The configured-path audit passed; Content stayed unchanged.
