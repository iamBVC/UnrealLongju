# Fishing

Implemented 2026-10-08 against UE 5.7.4. Gameplay is authoritative on the map
server; visual-water meshes are not used to decide whether a spot is fishable.

## Playing and configuration

Equip a rod by right-clicking it, then right-click a bait item. Press the attack
key (Space with native input) once to cast, and again to reel in. The fishing
skill (legacy vnum 123) routes to the same logic. Moving cancels the attempt.

The initial wait is 10–40 seconds. A loot/notification-log message and the character's reaction
animation indicate a bite. Reel in within six seconds. Timing is probabilistic:
normal catches peak around three seconds, slow ones around five, quick ones
around one; rod plus bait power is checked against the catch's difficulty.
Responding within the window is not a guaranteed catch.

**Project Settings > Metin2 > Metin2 Fishing** controls the wait, cast distance,
movement tolerance, dropped-reward ownership time, permitted map/table pairs,
catch weights, timing profiles, and fisherman NPC vnums. Values live in
`Config/DefaultGame.ini`, under `[/Script/Metin2.MT2FishingSettings]`.
The default permitted map indices are 1/21/41 (table 0) and 3/23/43 (table 1),
matching the legacy town rules. Add explicit rules for other maps.

The 37 rows and five timing profiles come from the local legacy server's
`share/locale/italy/fishing.txt` and `game/src/fishing.cpp`. Weights total
9950/9950/9800/9900. A null `ItemTemplate` row is a miss; other rows select fish
and items by soft item-class reference, editable in Project Settings. The server
loads the selected class and derives its numeric identity only for the existing
inventory/save format. Fishing result events also carry the item class, not a vnum.
Old `CatchTable` overrides using `Vnum` must be changed to `ItemTemplate` references.
No runtime access to that legacy directory is required. Tables 2 and 3 are
available to configure; premium/event switching, regional suppression of gold
and disguise rewards, fishing-event leaderboards, fish opening and grilling
are not part of this first gameplay pass.

## Server validation and inventory

`UMT2FishingComponent` accepts intent plus a session token, not a client catch,
reaction timestamp, hook coordinate, item class, or random seed. The server:

* Requires a living, grounded, dismounted player with an equipped rod and bait.
* Uses the registered map's original attribute grid, rejects no-walk ground,
  and searches water-bit cells 600 cm away in the legacy ±10-degree sweep.
* Rechecks the same map, water cell, equipment, and movement at bite and reel.
  A small active-only timer also detects movement, mounting, and invalidation.
* Owns the bite deadline and both success rolls. Stale requests cannot resolve
  another cast. Normal attacks and skills cannot bypass rod restrictions.
* Consumes bait on a resolved/expired bite, including a cancellation after the
  bite. Cancelling before the bite retains it. Equipment changes cancel before
  transferring the original rod, so swapping cannot retain spent bait.
* Grants one actual item instance. If inventory is full it creates an
  owner-protected world item instead. Fish length uses the legacy normal/rare
  size distributions and remains part of the item instance.

Timers are cleared on teardown. Fishing sessions do not persist across travel,
death or disconnect. Rod proficiency, bait and fish length use existing numeric
socket columns: no new database schema or asset-version fields were introduced.
Equipment Metin stones keep their existing encoding.

## Rod progression and fisherman

Legacy Value0 is fishing power, Value1 is the practice-roll denominator, Value2
is the proficiency cap, Value3 is upgrade success percent, and Value4 is the
failed-upgrade result vnum. `FishingDelayTenths` retains its historical property
name for saved-asset compatibility, but is interpreted as Value0 power.

Reeling after a bite can raise socket 0 proficiency by one, on successes or
failures, until the cap. It does **not** automatically replace the rod with the
next level: the legacy fisherman upgrades a fully trained, unequipped rod.
The `__fish_real_refine_rod` quest binding verifies the nearby configured NPC,
offered slot and unchanged server snapshot after the dialog, then atomically
replaces the item on success/downgrade and resets its sockets. Invalid requests
return legacy result 2 and preserve the item.

The 20 imported rod templates have been refreshed with previously omitted
practice and downgrade values. To refresh only those templates again:

```powershell
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $ProjectFile `
  -run=MT2ImportItems -FishingRodsOnly -nullrhi -nosound -unattended -nop4
```

## Animation and presentation boundary

All eight player AnimBPs bind the nine existing `fishing` actions. Native idle
switches to `fishing_wait` during a cast, with one-shot throw/react/catch/fail/
cancel montages through `DefaultSlot`. Nearby clients receive fishing events;
late joiners can reconstruct waiting state from replicated phase/hook data.
Rods now attach their imported item mesh to the character's weapon socket.
`RodMesh` in Fishing settings supplies the existing imported rod mesh when an
older item template has no world-mesh reference; a template's explicit mesh wins.
The local float uses the replicated hook XY and baked visual-water elevation,
including the global water offset, rather than the character's capsule height.
It has no collision, navigation influence, or server mesh loading.
The float bobs vertically with a slow sine wave while waiting, then dips smoothly
on the server's bite event. Motion is client-side only and ticks only while the
float exists; no movement is replicated or used for fishing validation.
`FloatBobAmplitude` (1.5 cm), `FloatBobPeriod` (4 s), `FloatBiteDipDepth` (12 cm)
and `FloatBiteDipResponseTime` (0.15 s exponential response) are configurable
under Presentation > Float Motion. Idle/end-play cleanup disables the tick.
Float mesh, material, scale and height offset are configurable in Metin2 Fishing settings;
the initial visible float is a small engine-shape placeholder. The legacy float
uses an animated MDE mesh, which the existing effect importer does not yet
translate. Its exact model, icons, line effects and dedicated fishing UI
remain future presentation work.

Fishing sounds are already embedded as `UAnimNotify_PlaySound` events in the
imported animations. All eight variants have sound events for throw, catch,
failure and cancellation, imported from the legacy `.mss` motion scripts.
Python registers the motions but does not define their sound timings. Waiting
and bite-reaction animations have no sound events in the supplied assets.
The redundant native sound table and timer playback have been removed. Edit
sounds and their timings on the animation's Notify track; no Content packages
were modified by this cleanup.

Fishing and other system feedback now share the existing loot history. Player,
guild and administrator chat messages remain in the chat history. Existing
`AddInfoChatLine`/`SendSystemChatMessage` names are retained for compatibility,
but route to the notification log; guild text uses the separate player-chat path.

Body refreshes preserve the live AnimInstance when the skeletal mesh is unchanged,
so bait/proficiency replication cannot interrupt the finishing montage. Real
armor mesh changes retain the existing animation reinitialization behavior.

To refresh only fishing bindings, without regenerating combat animations:

```powershell
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $ProjectFile `
  -run=MT2GeneratePlayerAnimation -FishingOnly -nullrhi -nosound -unattended -nop4
```

Both commands save Content packages. Save/close the editor first and review the
Content submodule diff. Source and destination paths use the existing path
catalogue; `Part_fishing` selects the source asset subfolder.

## Validation

`Metin2.Fishing.LegacyRules` checks configuration, weight boundaries, timing
curves, rod power and length-sensitive stacking. `Metin2.Fishing.Authority`
checks synthetic server-attribute maps with actual imported rods, stale/duplicate
requests, bait, movement, expiry, catch delivery, proficiency caps, fisherman
refinement and numeric-socket persistence. Tests live in `Source/Metin2/Tests`.
Headless tests do not validate visual animation blending or remote latency.
The rules test loads every configured reward and checks sound events in all
32 bound throw/catch/fail/cancel animations;
`Metin2.UI.NotificationRouting` checks chat/notification separation and the
bounded shared loot history.
