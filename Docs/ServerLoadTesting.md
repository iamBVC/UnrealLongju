# Server load testing

Development builds provide the admin-only `/fakeplayers` command. Fake players are server-controlled warrior characters with a normal PlayerState, inventory, sword +9 (VNUM 19), movement and combat. They seek nearby monsters and Metin stones, use ordinary chase/combo/damage handling, collect nearby loot reserved for them, wander when no target is available and respawn after death. They are unavailable in Shipping builds.

```text
/fakeplayers 100
/fakeplayers 100 5000 50
/fakeplayers status
/fakeplayers clear
```

Arguments are count, optional radius in centimetres and optional level. Defaults are a 3000 cm radius and level 1. Creation is staggered; the command reports queued count and a completion summary, including failures to find usable ground or equip the sword. The default limit is 200, including queued requests. Clear removes only fake players and cancels pending spawns in the current world.

Configure **Project Settings > Game > Server Load Testing**, or `[/Script/Metin2.MT2LoadTestSettings]` in `Config/DefaultGame.ini`. Limits, spawn batching, radius, level, target-search radius, decision interval and respawn delay are configurable. Warrior appearance and weapon VNUM 19 are fixed.

Fake players have no account/persistence identity and do not acquire admin rights. They do not exercise login, character loading or database saves. Their pawns are included in the player spatial grid, so they activate mobs and can be selected by monster AI. Characters and PlayerStates replicate normally to real clients observing them. Controller decisions have the `MT2.LoadTest.BotDecision` FramePro scope; the harness has `MT2.LoadTest.Tick`.

For admin mob placement, `/m <vnum> [count] [radius_cm]` spreads up to 20 mobs over a random disk around the player (radius 0..20000 cm), with ordinary ground/capsule placement checks. Without a radius, mobs use the standard nearby placement. Example: `/m 101 20 5000`.

## What this measures

- Server-side character movement/collision, attacks, mob reactions and ordinary combat/reward work.
- Mob activation and target queries with many player-like pawns.
- Replication preparation and delivery of those actors to the real clients actually connected.

It does **not** create fake network connections or client movement RPCs. One real client plus 100 fake players still has one real connection, not 101. Owner-only data, acknowledgements, prediction/correction and per-connection relevance work are not representative of 100 clients. AI decision work is also artificial overhead absent from human-controlled players.

Use this as a gameplay-load test, then use independent client processes with actual connections for network-capacity validation. Test both distributed players and dense combat, under latency/packet loss. Measure CPU percentiles, bytes/second per connection, correction rates, actor counts and replication work; do not extrapolate one connection into a 200-client capacity claim.

## Running a repeatable test

1. Start the packaged Development server cluster and connect a real admin client.
2. Capture a baseline, then spawn a chosen count/radius/level and wait for creation to complete.
3. Capture steady-state movement/combat separately from spawn-time asset loading. Keep the observing client in the test region if measuring its received actor traffic.
4. Use `status`, clear the fake players and reset the test world between comparable runs. Combat changes the world: killed monsters, loot and respawns are normal gameplay effects; clear does not undo them.

Low-level warriors can die frequently and idle between respawns; choose a representative level explicitly for sustained combat. Wandering uses the game's direct movement/collision, not a new navigation system. Blocked terrain can prevent reaching a chosen target.

Player capsules use query-only collision by default: terrain sweeps, targeting and portal overlaps remain active, but capsules do not participate as Chaos rigid bodies or push physics objects. Enable `bPlayerPhysicsInteraction` in Metin2 Gameplay settings only when rigid-body interaction is required, using matching client/server configuration and newly spawned players. Knockback retains Unreal root-motion-source prediction and replication. Local player-grid queries use neighbor buckets; large sparse searches scan occupied buckets. These settings do not replace real-client capacity testing.

Existing combat/engagement diagnostics can add logging overhead. For comparable CPU captures, set the same log verbosity in every run; for example `-LogCmds="LogMT2Combat Error,LogMT2Targeting Error"`. Restore diagnostic verbosity when investigating a failure.

Automation coverage is under `Metin2.Server.FakePlayers`. A successful fixture is not a live 100-client test.

Dedicated servers disable character skeletal ticks, clear cosmetic mesh/AnimBP resources and detach hair/weapon socket attachments. Combat still reads imported animation metadata; movement and knockback remain authoritative. PlayerState polling defaults to 2 Hz via `PlayerStateReplicationRate` in Metin2 Gameplay settings. Progression, appearance, vitals, primary stats, skill levels and gameplay-tag changes request immediate replication. The baseline remains a fallback for other subobject updates; equipment and PlayerState network layouts and relevancy are unchanged. Compare client-visible state under latency when changing this rate.
