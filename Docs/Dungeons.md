# Shared dungeon stages

Each map has its own server process. A dungeon uses one map and one `UWorld` containing its room meshes. Each placed `AMT2DungeonRoom` is a logical stage shared by every player in that stage: mobs, bosses, door state and encounter time are shared, not instanced per party.

`UMT2DungeonSubsystem` keeps world-local weak room, player and enemy mappings. Room IDs must be unique and nonempty within the world. A player/enemy cannot belong to two rooms simultaneously. No additional worlds, listeners or process topology are introduced.

## Authoring and lifecycle

Place one Dungeon Room actor per stage. Give it a unique `RoomId`, fit its `Bounds` to that room's actual meshes, and position its `Entrance` at a safe player spawn point inside those bounds. Bounds must not overlap other stages. Assign placed doors and an optional `NextRoom`. Door actors must have stable level references, or be replicated actors when created dynamically.

The lifecycle is `Idle → Active → Completed/Failed → Idle`. Players enter/leave through overlap events; authoritative quest/Blueprint code may also call `JoinPlayer` after verifying their position. Start the encounter explicitly with `StartEncounter` after gathering participants. Repeated starts cannot duplicate an active encounter.

`Spawns` uses imported mob VNUMs and room-local transforms, including capsule height. A failed spawn fails the encounter and cleans up the partial room-owned batch; it does not silently turn into a completed stage. Existing map regen must not also spawn the same encounter mobs. Alternatively, the authoritative encounter script can spawn mobs and call `RegisterEnemy` while active. Bosses use the same tracking.

Killing all tracked objectives completes the encounter and opens its doors. Destroying a living objective instead fails the encounter; despawn cannot bypass progression. Manual completion/failure hooks support later quest-driven objectives. Door collision/visibility and shared status are replicated; decorative door animation is not implemented.

The room owns its encounter deadline, independently of player quest timers. Late arrivals share that deadline instead of restarting it. Timeout fails the encounter and opens doors so occupants can leave. An occupied room never resets. Once empty, a configurable grace timer permits cleanup/reset; an admitted returning player cancels that timer. PlayerState and pawn teardown remove membership, and room teardown removes bindings and timers. Room actors have no per-frame Tick.

`AdvancePlayer` only works after completion, stays within the same world, and joins the configured next room after a collision-checked teleport. Invalid destinations fail without transferring ownership; failed membership transfer restores the source position. It does not migrate actors across server processes.

## Individual progress and rewards

Player quest flags, inventory, combat loot and EXP remain individual. Completion eligibility includes persistent character identities present when the encounter starts and players admitted while it is active. Players arriving after completion may share the stage and advance, but do not gain that run's completion entitlement. Leaving and rejoining does not clear an existing claim. This policy is separate from normal combat loot/EXP.

`TryClaimCompletion` consumes at most one entitlement per character per run, while that player is still present in the completed room. It does not itself grant items, EXP or money. Reward delivery must be attached to authoritative quest logic and checked for inventory/save failure; durable exactly-once rewards across process crashes are not implemented here.

`pc.in_dungeon()` now reflects logical room membership instead of always returning false. Other `d.*` quest commands and private-map guards are not automatically translated into working shared-stage behavior.

## First integration target

Devil Tower's existing world is `/Game/Maps/Game/devil_tower`. The legacy `deviltower_zone.quest` supplies stage coordinates, elimination-driven transitions and regen filenames, but also creates private maps and checks private-map index ranges. Those semantics require explicit conversion to shared rooms. The core does not rewrite that quest, guess mesh bounds, place room actors into the map, or claim that the complete tower is playable yet.

Next: author verified room bounds/entrances, replace the tower's private-map progression with room transitions, implement its required dungeon commands and room-owned server timers, then run a multi-client packaged test. `Metin2.Dungeons` covers native shared lifecycle and teardown; quest regression tests remain necessary. Client replication, navigation, imported regen integration and complete tower gameplay need packaged validation.
