# Documentation Status

Reviewed: 2026-10-05–2026-10-06.

## Scope and interpretation

The review covered all 37 existing project Markdown files: 36 in the parent repository and `Content/README.md` in the content submodule. Generated caches/build output and external dependency documentation are not project guidance. This index is an additional document created by the review.

Current guidance was cross-checked against source, target/module descriptors, configuration, scripts, and the content submodule. Original-game research was retained as historical material; external legacy datasets were not re-imported or re-audited. Source line numbers and machine-specific legacy paths identify the studied datasets, not files every checkout must contain.

Recorded build/test/import results apply only to the stated historical pass. This review did not run Unreal builds, commandlets, multiplayer, cooking, or quest playthroughs. An implementation inventory is not a gameplay-parity guarantee.

## Reviewed documents

| Documents | Review outcome / authority |
| --- | --- |
| [Root README](../README.md), [Content README](../Content/README.md) | Current repository names, recursive setup, content dependency, and third-party rights boundary. Rights notice retained; no asset permissions inferred. |
| [Architecture](Architecture.md), [ProjectStatus](ProjectStatus.md) | Current component boundaries and implemented foundations; removed already-completed roadmap entries and unsupported build claims. |
| [PersistenceArchitecture](PersistenceArchitecture.md), [DistributedServerArchitecture](DistributedServerArchitecture.md) | Eleven-table coordinator schema, compact player fields, threading distinction, local guild fallback, no automatic versioned migration, current launcher paths. |
| [CommandLineParameters](CommandLineParameters.md) | Runtime defaults/required values, packaged binary paths, current map example, tick/GC/voice flags, local Shipping launch behavior. |
| [Patcher README](../Patcher/README.md), [Localization](../Scripts/LOCALIZATION.md) | Explicit release versioning, placeholder/default endpoints, hash trust limits, correct script paths, parent/submodule ownership. |
| [MT2UE README](../Plugins/MT2UE/README.md), [Gr2ToObj](../Plugins/MT2UE/Tools/Gr2ToObj/README.md), [SpeedTreeToObj](../Plugins/MT2UE/Tools/SpeedTreeToObj/README.md) | UE5 editor integration, functional versus stub import domains, optional x86 helper prerequisites, separate SpeedTree bridge. |
| [QuestSystem](OldGameResearch/QuestSystem.md), [QuestPortingStatus](OldGameResearch/QuestPortingStatus.md), [NPCAndQuestSystem](OldGameResearch/NPCAndQuestSystem.md), [GuildAndTimers](OldGameResearch/GuildAndTimers.md) | Current executor/results/entity identity and coverage; retained dated audit history and original quest model; distinguished local JSON guild API from coordinator authority. |
| [SkillBooks](OldGameResearch/SkillBooks.md), [SkillSystem](OldGameResearch/SkillSystem.md) | Current configurable Master-book EXP/cooldown policy, implemented grand-master backend, remaining quest fidelity, original versus port skill paths. |
| [MessengerSystem](OldGameResearch/MessengerSystem.md), [TargetBoard](OldGameResearch/TargetBoard.md), [SystemMenu](OldGameResearch/SystemMenu.md), [TargetAndSelectEffect](OldGameResearch/TargetAndSelectEffect.md) | Current messenger layout/bindings, online/offline policy, generation scope, target-board wiring, settings and casting ownership; original UI research retained. |
| [ItemsAndInventory](OldGameResearch/ItemsAndInventory.md), [ItemTooltips](OldGameResearch/ItemTooltips.md), [MobProtoFormats](OldGameResearch/MobProtoFormats.md) | Current 156-byte MIPX item reader, historical formats distinguished, four-page inventory, damage-value convention, socket support, 255-byte mob format constraints. |
| [MapCoordinateSystem](OldGameResearch/MapCoordinateSystem.md), [SafeZones](OldGameResearch/SafeZones.md) | Implemented coordinate/rotation/town-spawn reflection, authoritative BANPK policy, historical source fallback and validation counts. |
| [MetinStones](OldGameResearch/MetinStones.md), [Knockback](OldGameResearch/Knockback.md), [DamageFeedback](OldGameResearch/DamageFeedback.md), [DamageDistributionAndAoE](OldGameResearch/DamageDistributionAndAoE.md), [AttackSpeedAndMovement](OldGameResearch/AttackSpeedAndMovement.md) | Existing stone/group/linkage, damage types/colors, player CRUSH path, current symbols; no blanket legacy timing/physics parity claim. |
| [MountSystem](OldGameResearch/MountSystem.md), [DeathAndRespawn](OldGameResearch/DeathAndRespawn.md), [AdminGMSystem](OldGameResearch/AdminGMSystem.md), [AffectSystem](OldGameResearch/AffectSystem.md), [PotionsAndAffects](OldGameResearch/PotionsAndAffects.md) | Current component/authority/persistence mappings; corrected obsolete mount-handler/admin-shard descriptions; retained original behavior and explicitly incomplete progression/respawn fidelity. |

## Evidence and unresolved work

The latest recorded quest baseline is 238 scripts, 237 refreshed Blueprints, 6,595 triggers, 27,761 nodes, and 576 diagnostics: **482 unconverted statements, 67 failed gates, 27 unsupported triggers**. Consult the audit's later sections for superseded limitations; earlier test counts are not the latest suite count.

The checked-out content commit includes starter assets. This is not an asset-completeness, cookability, or licensing audit. Use the parent-pinned revision and validate the specific build's required maps/classes/registry data.

Known operational gaps include unpinned engine source, automatic schema migrations, scheduled backup/retention, production transport security, and complete quest/gameplay fidelity. Do not remove these warnings merely because a build or import succeeds.

## Keeping documentation current

- Update the affected guide with code/configuration changes, not only the top-level README.
- Separate runtime defaults from launcher overrides and replaceable examples.
- Date measurements and identify their corpus, target/configuration, and validation limitations.
- Keep historical research labeled; do not promote an earlier milestone into a current claim.
- Verify local links and symbols, then review the Markdown diff and `git diff --check`.
- Commit submodule changes separately and push them before recording the parent pointer. Documentation edits do not authorize publishing assets or changing their licenses.
