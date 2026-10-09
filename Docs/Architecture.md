# Runtime Architecture

## Principles

- Use Unreal Engine framework classes, replication, movement, components, and subsystems.
- The dedicated server owns gameplay state, validation, simulation, and persistence.
- The client owns input, presentation, camera, UI, prediction, and interpolation.
- Never trust client-provided results. Clients send intent; the server validates and applies it.
- Keep framework classes small. Add gameplay features through focused actor components and subsystems.

## Foundation Classes

- `UMT2GameInstance`: process lifetime services and connection flow. It does not own replicated match state.
- `AMT2GameModeBase`: server-only rules, login, spawning, and map-specific policy.
- `AMT2GameStateBase`: replicated state visible to all connected players.
- `AMT2PlayerState`: replicated player identity and public gameplay state that survives pawn replacement.
- `AMT2PlayerController`: player command boundary, local UI ownership, and validated server requests.
- `AMT2CharacterBase`: shared replicated character representation and common character components.
- `AMT2PlayerCharacter`: player-specific camera and control behavior.

## Gameplay Framework

- Enhanced Input owns device mappings and input actions. Native mappings provide a playable fallback; a `UMT2InputConfig` data asset can replace them without changing character code.
- Player `ACharacter` movement provides client prediction and server correction. Mobs use timed movement segments and local interpolation with temporary swept snapshot movement during knockback; see [NPC movement](OldGameResearch/NpcMovementReplication.md).
- The Gameplay Ability System owns attributes, effects, abilities, costs, cooldowns, and gameplay tags.
- The player Ability System Component lives on `AMT2PlayerState`, so it survives pawn replacement and map travel.
- `AMT2PlayerCharacter` is the current avatar and initializes the Player State as the ability owner.
- Core attributes are server-owned and replicated. Clients consume them for presentation only.
- Health is public replicated state; mana, stamina, experience, and skill levels replicate only to the owning player.
- Character name, level, race, sex, and style are public Player State identity fields.
- Empire is public replicated identity: Shinsoo, Chunjo, or Jinno.
- Character visuals resolve from configurable soft mesh and skin references, then load asynchronously on clients only.
- `UMT2CharacterAppearanceComponent` owns asynchronous mesh, skin, and animation-class application. Player characters only forward replicated appearance identity to it.
- Animation Blueprint classes are selected by race and sex through `UMT2CharacterAppearanceSettings`; style changes only the skin.
- Player Animation Blueprints derive from `UMT2CharacterAnimInstance`, which provides movement direction, speed, falling, acceleration, and death state in native code.
- MT2UE's `Generate Player Animation Blueprints` command imports only missing base wait/walk/run clips, binds them directly to each player mesh skeleton, and creates eight non-destructive race/gender AnimBPs.
- Generated locomotion blends wait, walk, and run, then passes through `DefaultSlot` for attacks and other montages. Equipment selects weapon-specific animation sets; imported motion coverage and combo fidelity still require content validation.
- Locomotion playback and `ACharacter` movement use the replicated GAS `MovementSpeed` attribute, where 100 is the default multiplier. Gameplay Effects from equipment or consumables can modify the same attribute.
- `UMT2EquipmentComponent` owns modular hair, attached weapon, and body-armor presentation. Weapon bones follow the legacy race mapping; body armor uses race/sex variants imported from MSM shape tables.
- Holding the primary attack input repeatedly activates the GAS attack ability. `UMT2CombatComponent` owns timing and the configured combo index/length; releasing input stops new attacks.
- Health, mana, and stamina are exposed through reusable GAS-backed actor components on `AMT2CharacterBase`; the Attribute Set remains with the owning ASC.
- `UMT2PrimaryStatsComponent` owns reusable ST/DX/HT/IQ base and bonus values. Player base values live on Player State and persist; mobs receive them from imported proto data.
- `UMT2CombatStatsComponent` owns base and calculated defense, damage range, attack speed, movement speed, attack range, and maximum health. Equipment and effects will contribute through its bonus layer.
- Skill levels live in `UMT2SkillComponent`. Players attach it to Player State for pawn-independent persistence; future mobs can attach the same component to their actor.
- Spacebar requests the tagged primary attack ability. Jumping is disabled.
- Basic attacks activate through GAS, resolve a short pawn sweep on the server, validate visibility, and apply an instant damage effect.
- Map mob populations are owned by one level spawn actor with a reusable spawn component. Spawn definitions contain only mob VNUMs and are generated from server regen/group files during map import.
- `UMT2VnumRegistrySubsystem` loads the cooked `/Game/Logic/DA_MT2VnumRegistry` asset once. It never scans project assets during PIE or server startup.
- MT2UE rebuilds the registry after mob/item Blueprint compilation, save, add, remove, or rename. Duplicate VNUM validation preserves the last valid registry instead of writing ambiguous entries.
- Mob rewards use editable loot-entry arrays on each mob Blueprint and can produce multiple item results from one death.
- Static item definitions are Blueprint class defaults derived from `UMT2ItemTemplate` and specialized template classes such as weapon and armor. `UMT2Item` stores the template class, count, bonuses, sockets, and instance GUID.
- Raw damage enters a non-replicated meta attribute; only resulting health and the replicated `Status.Dead` tag leave the server.
- Skill levels use sparse Fast Array replication and gameplay tags instead of a fixed 255-entry legacy array.
- Core GAS effects/tags coexist with `UMT2StatusEffectComponent`'s replicated legacy-affect records and behavior definitions. Legacy duration, SP drain, recovery pools, and persistence are not represented solely by GAS tags.

## Target Layout

- `UnrealLongjuEditor`: editor development and asset workflows.
- `UnrealLongjuClient`: player client without server-only executable code.
- `UnrealLongjuServer`: headless authoritative dedicated server.
- `UnrealLongju`: convenient standalone game target for local development.

## Extension Boundaries

Continue gameplay as small vertical slices. Login/selection, combat, inventory, persistence, NPC/quest, party, and guild code already exists; do not treat this list as unimplemented features. See [ProjectStatus](ProjectStatus.md) and [QuestPortingStatus](OldGameResearch/QuestPortingStatus.md) for current gaps and recorded validation.

Persistent account and character data belongs behind a server-only service boundary. Unreal replicated actors contain live session state, not the database implementation.

Shared definitions should use data assets or data tables. Runtime ownership and mutation remain on the server. UI reads replicated or locally predicted presentation models rather than owning gameplay state.
