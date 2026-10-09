# NPC movement and replication

## Verified legacy behavior

- `game/src/config.cpp`: defaults to 25 pulses/second, view range 5000 plus a 500-unit margin.
- `CHARACTER::Goto` records the destination and schedules move-state updates every four pulses.
- `CHARACTER::CalculateMoveDuration` stores the starting XY position, start time and duration.
- `CHARACTER::StateMove` samples the timed XY segment rather than running capsule/terrain physics.
- `SendMovePacket` sends action, destination, rotation, time and duration to the entity view list.
  It does not broadcast a transform for every server movement step.
- `EncodeInsertPacket` supplies the remaining movement command to a newly observing client.
- `NetworkActorManager::SetDstPosition` / `UpdatePosition` interpolate the segment locally.
- `SECTREE::DecreasePC` stops state machines when neighbouring sectors have no players.
- Player movement is not purely event-only: start/stop notifications are supplemented by moving
  notifications every 300 ms in `PythonPlayerEventHandler.cpp`.

## Unreal implementation

- `UMT2MobMovementComponent` replicates a segment: start/destination, server start time, duration,
  moving/external-motion flags and serial. It changes at command/retarget/stop boundaries, not
  every movement step. Repeated requests for the same chase destination do not resend it.
- Ordinary mob locomotion disables actor movement replication and bypasses the heavyweight
  CharacterMovement tick. Authority samples the segment at 6.25 Hz by default; clients sample
  it every rendered frame using synchronized GameState time. Velocity is maintained for animation.
- Ground projection, swept capsule collision and the cooked no-walk grid remain authoritative.
  A blocked movement or missing ground terminates the segment and publishes the actual endpoint.
  This intentionally retains Unreal world collision instead of copying the legacy server's flat XY-only model.
- Knockback uses the same segment path with the legacy ease-out curve and preserved victim facing.
  Completing the impulse, hitting an obstruction or dying publishes a stopped segment at the actual
  location. Native movement/root-motion replication stays disabled throughout mob locomotion.
- The frequent mob scheduler iterates an active set instead of checking all registered mobs.
  Existing spatial-relevance region occupancy removes/adds members and resets their next AI deadline.
  The relevance refresh still runs once per second and scans registered relevance components;
  this is not yet a complete port of the legacy sector entry/exit notification system.
- Inactive mobs enter network dormancy when they were awake. Reactivation wakes/flushed replication;
  NPCs already configured dormant are not mistakenly awakened. In-flight impulses cannot be suspended.
- Mob visibility uses `NetCullDistance=5500` through both actor instances and Replication Graph class
  settings, overriding individual Blueprint cull defaults.
  Simulation activation stays broader than visibility, reflecting the legacy neighbour-sector distinction.

## Settings and compatibility

Project Settings > Game > Mob Runtime / `Config/DefaultGame.ini`:

- Mob simulation always uses legacy state deadlines instead of distance-band decision rates.
- `LegacyPulseRate=25`, `LegacyMovePulses=4`: 160 ms movement/chase sampling. Unreal's server tick
  quantizes these deadlines; this is not a replacement of Unreal's clock with a 25 Hz legacy clock.
- Idle aggressive mobs wait a random 1-3 seconds, passive mobs 3-5 seconds, and wander arrivals
  1-3 seconds. Defaults match `char_state.cpp`; each range is configurable.
- `LegacyWanderOneIn=7`: an idle decision has a one-in-seven wandering chance; the new destination
  is 300-700 cm from the current position. Midpoint and destination must pass the no-walk grid.
- Ordinary chases keep the issued destination until arrival; bosses have a configurable one-in-four
  retarget chance per moving decision. Existing Unreal leash, acceptance-radius and target rules remain.
- Client movement ticks follow replicated segment flags rather than independently replicated AI state,
  preventing stale idle/attack state updates from suspending a newer command. Authority only waits on
  enabled, unexpired segments; suspended or expired commands can be reissued without a fixed timeout.
- Combat decisions wait for the existing motion/cooldown timers rather than polling at the near rate.
  Target changes wake sleeping AI. This does not port race-specific boss, party/protege or stone AI branches.
- `BaselineMobReplicationRate=1`: Unreal property polling fallback, not a legacy packet frequency.
  Movement commands, state changes, health/max-health changes and actions force updates. Mob grid
  lists honor these forced updates and configured periods instead of engine distance/view-angle zones.
- Knockback uses a timed ease-out segment at the same baseline polling rate, with forced start/stop
  updates. Observers evaluate the trajectory locally; mobs do not stream native movement/root-motion
  snapshots during a shove. Capsule sweeps and no-walk checks remain authoritative.
- `ReplicationGridCellSize=6400`: replication partition size only; it does not replace the existing
  simulation-region occupancy system with the legacy nine-neighbour-sector implementation.
- `MovementRetargetDistance=50`: suppresses small chase-destination changes; speed/acceptance changes
  still restart the segment. This is a UE tuning tolerance, not a copied legacy constant.
- `NetCullDistance=5500`: observer visibility distance in imported-map centimetres.

Clients and dedicated servers must use matching network definitions. Stock player movement remains authoritative
with owning-client prediction; player observer traffic needs a separate measured optimization pass.

## Validation

`Metin2.World.MobMoveSegments` covers cadence, unchanged-request suppression, packet-free movement
steps, client catch-up, client authority rejection, stopping, walls, no-walk cells, active-set/dormancy
wake-up, eased shove timing, facing, ground stability and late-observer convergence. Existing
combat/knockback timing tests remain relevant.

`Metin2.World.LegacyMobScheduling` covers configurable pulse cadence, aggressive/passive idle
deadlines, sleeping-decision suppression, event wake-up and event-driven knockback polling.
These fixtures do not exercise actual packets across multiple network connections.

A packaged-server capture and multiplayer playtest are required to measure actual CPU/bandwidth
improvements and verify visual interpolation under latency, relevancy re-entry and dense combat.
Passing these unit tests does not establish 200-player capacity.
