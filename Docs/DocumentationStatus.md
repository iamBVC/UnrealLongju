# Documentation guide

The documentation describes the current UnrealLongju codebase and the original Metin2 behavior used as a porting reference. It is not a changelog or a guarantee of complete gameplay parity.

## Start here

- [Setup and build instructions](../README.md): cloning, engine setup, content, packaging and troubleshooting.
- [Project status](ProjectStatus.md): implemented systems and remaining work.
- [Architecture](Architecture.md), [persistence](PersistenceArchitecture.md) and [distributed servers](DistributedServerArchitecture.md): ownership, components, storage and process boundaries.
- [Command-line parameters](CommandLineParameters.md): runtime roles and configuration.
- [Automation tests](../Source/Metin2/Tests/README.md): test organization and execution.
- [Server load testing](ServerLoadTesting.md): admin-controlled fake players and real-client benchmark limits.

## System guides

- [Area painting](AreaPainting.md) and [water](Water.md): gameplay attributes, visual water and editor authoring.
- [Fishing](Fishing.md), [duels](Duels.md) and [PIE travel](PIETravel.md): gameplay rules, presentation and editor behavior.
- [Quest system](OldGameResearch/QuestSystem.md) and [quest porting status](OldGameResearch/QuestPortingStatus.md): executor behavior, supported subsets and unresolved work.
- [NPC movement and replication](OldGameResearch/NpcMovementReplication.md): legacy movement rules and their Unreal implementation.
- Other documents under `OldGameResearch/` describe original-game data and behavior alongside the corresponding current port.

Content and import tooling have separate guidance in the [Content README](../Content/README.md), [MT2UE README](../Plugins/MT2UE/README.md) and its tool READMEs. Release tooling is documented in the [patcher guide](../Patcher/README.md) and [localization guide](../Scripts/LOCALIZATION.md).

## Reading the guides

Legacy source paths identify external reference datasets; they are not files supplied by every clone. Asset availability depends on the parent-pinned content submodule revision. Rights notices do not establish redistribution permission.

Implemented code, successful builds and successful conversion are different from verified gameplay parity. Use the generated conversion report for the actual imported quest corpus, and validate the intended content in multiplayer and cooked builds. Engine revisions, production transport security, database migration/backup procedures and unresolved gameplay backends require explicit review.

## Keeping documentation current

- Explain current behavior, defaults, ownership, configuration and limitations.
- Keep original Metin2 behavior clearly distinguished from the Unreal implementation.
- Remove obsolete implementations, milestone narratives, resolved bug stories and dated build/test tallies.
- Document useful test commands and coverage without treating old results as current proof.
- Retain compatibility constraints only when they still affect the current code or supported assets.
- Verify paths, symbols and local links; review the diff and whitespace.
- Preserve third-party rights notices. Content-submodule changes require their own review and publication workflow.
