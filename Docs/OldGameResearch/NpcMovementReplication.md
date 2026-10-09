# NPC movement and replication

Implementation review: 2026-10-08. Player prediction/networking is unchanged by this stage.

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
- Knockback temporarily restores the existing full-rate swept root-motion/snapshot path. Completing
  the impulse or dying publishes a stopped segment and disables ordinary actor movement replication
  again. Client smoothing offsets are cleared when returning to segment presentation.
- The frequent mob scheduler iterates an active set instead of checking all registered mobs.
  Existing spatial-relevance region occupancy removes/adds members and resets their next AI deadline.
  The relevance refresh still runs once per second and scans registered relevance components;
  this is not yet a complete port of the legacy sector entry/exit notification system.
- Previously awake, inactive mobs enter network dormancy. Reactivation wakes/flushed replication;
  NPCs already configured dormant are not mistakenly awakened. In-flight impulses cannot be suspended.
- Mob visibility uses `NetCullDistance=5500` through both actor instances and Replication Graph class
  settings, so previously baked Blueprint cull defaults do not silently retain the old 12000-unit radius.
  Simulation activation stays broader than visibility, reflecting the legacy neighbour-sector distinction.

## Settings and compatibility

Project Settings > Game > Mob Runtime / `Config/DefaultGame.ini`:

- `MovementSimulationRate=6.25`: authoritative collision sampling, not client animation rate.
- `MovementRetargetDistance=50`: suppresses small chase-destination changes; speed/acceptance changes
  still restart the segment. This is a UE tuning tolerance, not a copied legacy constant.
- `NetCullDistance=5500`: observer visibility distance in imported-map centimetres.

The network schema changes: clients and dedicated servers must be rebuilt/deployed together.
No imported asset packages or map files are regenerated. Stock player movement remains authoritative
with owning-client prediction; player observer traffic needs a separate measured optimization pass.

## Validation

`Metin2.World.MobMoveSegments` covers cadence, unchanged-request suppression, packet-free movement
steps, client catch-up, client authority rejection, stopping, walls, no-walk cells, active-set/dormancy
wake-up and the impulse networking handoff. Existing combat/knockback timing tests remain relevant.

A new packaged-server capture and multiplayer playtest are required to measure actual CPU/bandwidth
improvements and verify visual interpolation under latency, relevancy re-entry and dense combat.
Passing these unit tests does not establish 200-player capacity.
