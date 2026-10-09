# Guild system, quest timers, and item-socket scripting

Built to close the quest-conversion gaps that needed real game features rather than a better parser
(see Docs/OldGameResearch/QuestSystem.md).

## Guild system

The subsection below describes the local `UMT2GuildSubsystem` fallback, not authoritative coordinator guild operations. Cluster guild state lives in SQLite `guilds`, `guild_members`, and `guild_ranks`, with 15 editable ranks and `UMT2GuildComponent` player-facing state. The local JSON subsystem remains in source and must not be mistaken for the cluster store. See [Persistence Architecture](../PersistenceArchitecture.md).

`UMT2GuildSubsystem` (GameInstance) owns every guild on the server. Guild records live in the
subsystem, not on the player, so a guild keeps existing while its members are offline; a player's
membership is mirrored onto `AMT2PlayerState` (`GuildId`/`GuildName`) so nameplates and the character
window keep working unchanged.

- `FMT2Guild`: id, name, level, experience, master character id, members, notice, ladder points.
- `FMT2GuildMember`: character id + name, level, rank, contributed experience. Members are keyed by
  **persistent character id**, not by name or a live pointer, so records survive logout and renames.
- `EMT2GuildRank`: Member / Officer / Master. The old game had 15 configurable grades; only the three
  that carry authority were modelled in that older local API. This is not the current cluster/UI rank model.

Operations return `EMT2GuildResult` (rather than a bare bool) so callers can report the real reason:
create (level 40+, unique name), add/remove member, set rank, transfer mastery, disband, set notice,
and `AddGuildExperience` which consumes level thresholds one at a time so a single large contribution
can cross several levels.

Two rules worth noting: the master cannot simply leave (`CannotRemoveMaster`) - mastery is handed over
or the guild is disbanded, so a guild is never left headless; and mastery is only transferred through
`TransferMastery`, never assigned via `SetMemberRank`.

Member capacity is `10 + 2 * level`. Guilds persist as `Saved/MT2Guilds.json`, and ids are never reused
after a restart (`next_id` is saved).

Quest bindings: `guild.get_level`/`guild.level`, `guild.get_name`/`guild.name`, `guild.get_id`,
`guild.is_guild_master`, `guild.get_rank`, `guild.get_member_count`, `guild.get_ladder_point`.
Guild *wars* (ladder, betting, war maps) are not implemented - they are a separate feature with only a
handful of quest uses.

## Quest timers

The old scripts schedule named timers and handle them with a matching trigger:

```
server_timer('devilcatacomb_45m_left_timer', 60 * 15, d.get_map_index())
...
when devilcatacomb_45m_left_timer.server_timer begin ... end
```

Timers therefore need **name-matched triggers**, which the trigger model did not have. `FMT2QuestTrigger`
gained `TriggerName`, and the manager gained `DispatchNamedEvent`. Matching is strict in both
directions: a named event never falls into an unnamed trigger of the same type, so one timer's handler
can never run for a different timer.

- `UMT2QuestNode_StartTimer` - name, delay (literal or expression), `bServerTimer`, `bLooping`, and an
  argument expression. Re-using a name replaces the pending timer, as the old scripts expect.
- `UMT2QuestNode_ClearTimer` - cancels; an empty name clears every timer the quest owns.
- `get_server_timer_arg()` reads the firing timer's argument (`ActiveTimerArgument`, set for the
  duration of the handler).

Timers are scheduled per player on the quest manager, because quest execution is per player. A truly
server-wide timer only becomes meaningful once dungeon instances exist to own it.

## Item sockets as quest storage

Sockets were already implemented for Metin stones. The quests additionally use them as **scratch numeric
storage on quest items** (`item.set_socket(1, get_global_time())`), independent of any stone. So
`FMT2MetinSocket` gained an additive `Value` field:

- `item.get_socket(i)` reports the mounted stone's vnum when there is one, otherwise the scripted value.
- `item.set_socket(i, v)` writes the value through `UMT2InventoryComponent::SetItemSocketValue`, growing
  the socket array on demand. A stone mounted later still takes precedence when reporting, so this can
  never disturb real socket contents.

This required the quest context to know *which* item an event concerned: `FMT2QuestContext::EventItemSlot`
is set when `UseItem` dispatches, which also makes the old `when <vnum>.use` triggers work - quests now
get first refusal on a used item, and a quest that handles it consumes the use entirely so a quest item
never also runs the generic consumable path.
