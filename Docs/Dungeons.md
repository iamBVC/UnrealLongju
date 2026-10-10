# Shared dungeon stages

Each map has its own server process. A dungeon uses one map and one `UWorld` containing its room meshes. Each placed `AMT2DungeonRoom` is a logical stage shared by every player in that stage: mobs, bosses, door state and encounter time are shared, not instanced per party.

`UMT2DungeonSubsystem` keeps world-local weak room, player and enemy mappings. Room IDs must be unique and nonempty within the world. A player/enemy cannot belong to two rooms simultaneously. No additional worlds, listeners or process topology are introduced.

## Authoring and lifecycle

Place one Dungeon Room actor per stage. Give it a unique `RoomId`, fit its `Bounds` to that room's actual meshes, and position its `Entrance` at a safe player spawn point inside those bounds. Bounds must not overlap other stages. Assign placed doors and an optional `NextRoom`. Door actors must have stable level references, or be replicated actors when created dynamically.

The lifecycle is `Idle → Active → Completed/Failed → Idle`. Players enter/leave through overlap events; authoritative quest/Blueprint code may also call `JoinPlayer` after verifying their position. Start the encounter explicitly with `StartEncounter` after gathering participants. Repeated starts cannot duplicate an active encounter.

`Spawns` uses imported mob VNUMs and room-local transforms, including capsule height. A failed spawn fails the encounter and cleans up the partial room-owned batch; it does not silently turn into a completed stage. Existing map regen must not also spawn the same encounter mobs. Alternatively, the authoritative encounter script can spawn mobs and call `RegisterEnemy` while active. Bosses use the same tracking.

Killing all tracked objectives completes the encounter and opens its doors. Destroying a living objective instead fails the encounter; despawn cannot bypass progression. Manual completion/failure hooks support later quest-driven objectives. Door collision/visibility and shared status are replicated; decorative door animation is not implemented.

The room owns its encounter deadline, independently of player quest timers. `EncounterSeconds=0` disables the deadline for untimed encounters. Late arrivals share an active deadline instead of restarting it. Timeout fails the encounter and opens doors so occupants can leave. An occupied room never resets. Once empty, a configurable grace timer permits cleanup/reset; an admitted returning player cancels that timer. PlayerState and pawn teardown remove membership, and room teardown removes bindings and timers. Room actors have no per-frame Tick.

`AdvancePlayer` only works after completion, stays within the same world, and joins the configured next room after a collision-checked teleport. Invalid destinations fail without transferring ownership; failed membership transfer restores the source position. It does not migrate actors across server processes.

## Individual progress and rewards

Player quest flags, inventory, combat loot and EXP remain individual. Completion eligibility includes persistent character identities present when the encounter starts and players admitted while it is active. Players arriving after completion may share the stage and advance, but do not gain that run's completion entitlement. Leaving and rejoining does not clear an existing claim. This policy is separate from normal combat loot/EXP.

`TryClaimCompletion` consumes at most one entitlement per character per run, while that player is still present in the completed room. It does not itself grant items, EXP or money. Reward delivery must be attached to authoritative quest logic and checked for inventory/save failure; durable exactly-once rewards across process crashes are not implemented here.

`pc.in_dungeon()` now reflects logical room membership instead of always returning false. Other `d.*` quest commands and private-map guards are not automatically translated into working shared-stage behavior.

## Devil Tower integration

The world `/Game/Maps/Game/devil_tower` contains three authored, non-spatial `AMT2DevilTowerRoom` actors for the opening stages. Their bounds come from the actual imported room meshes. Entrances use the legacy warp/`special.devil_tower` coordinates with the verified 76800-cm X mirror. Positions and capsule clearance are baked against triangle collision on the room surface. The shared room mesh has explicit complex-as-simple collision with regenerated physics data; a single convex hull cannot represent the hollow arena.

1. `DevilTower.Floor1`: the 8015 entry stone is room-owned. Ambient first-floor groups retain their normal scheduler. Destroying the stone advances all current room members after six seconds.
2. `DevilTower.Floor2`: 205 mobs from `deviltower2_regen.txt`, including group membership and forced aggression. Complete elimination advances current members after four seconds.
3. `DevilTower.Floor3`: 260 mobs from `deviltower3_regen.txt`, including group 1024's 1091 boss. All tracked mobs must die. Progression currently stops here; floor four is not configured.

Admission starts an idle encounter on the next timer tick, after possession/teleport finishes. The opening stages have no failure deadline, matching the legacy quest. Transition timers belong to the room, not the killer, so one participant's disconnect cannot cancel the group's advance. Collision-blocked transfers leave that player in the source room and log a warning. These rooms have no separate door actors: floor changes use same-world teleportation.

On this configured map only, quest dispatch bypasses `deviltower_zone`'s old private-map progression and login guard. Its entrance NPC dialogue outside the tower remains available. Other quests are unaffected. Runtime needs only cooked assets, not the legacy source files. Reimporting the map can restore the ambient entry stone; rerun the configuration commandlet afterwards to avoid duplicate stones.

The editor commandlet `-run=MT2ConfigureDevilTower` loads World Partition actors for inspection. Supplying `-LocaleRoot=<server share/locale/italy>` and `-DungeonRoot=<server share/data/dungeon>` validates the opening rooms, imported VNUM classes, regen inputs and clear floor positions. Add `-Apply` to save only the tower rooms, ambient stone removal and room mesh collision package, or `-Verify` to check the saved room links, transforms, spawns, collision policy and ambient stone removal without saving. Validation happens before persistent writes; reruns update the same room IDs rather than duplicating actors. Spawn positions are deterministically baked inside source regen rectangles and avoid wall caps and occupied capsule space.

Still required: floors 4–9 (real/fake stones, timed seals and keys, blacksmith interactions, map/key puzzles, final boss and exit), their shared quest commands and individual completion rewards. `Metin2.Dungeons` includes the native opening transition and disconnect regression, not a complete tower run. Packaged multi-client replication, navigation, cooking and visual gameplay remain unverified.
