# Messenger (friends, presence, private messages)

Source review: 2026-10-06. Original references: `game/src/messenger_manager.{h,cpp}`, `uimessenger.py`, `messengerwindow.py`, and `whisperdialog.py`. These are external legacy datasets, not checkout prerequisites.

## Original behavior and deliberate extension

The original friend graph was symmetric and consensual: request, accept, then write both directions. Removal was symmetric, and presence was pushed across game servers. Original whispers were live-only.

The port adds coordinator-persisted messages and offline delivery to friends. Online whispers do not require friendship. The old statement that only friends may message each other was incorrect for the current implementation.

## Authority and storage

| Concern | Current owner |
| --- | --- |
| Friend graph and stored messages | Coordinator SQLite backend. |
| Presence and pending friend requests | Coordinator in-memory state. |
| Player-facing friend snapshot | `UMT2MessengerComponent` on PlayerState, owner-only replication. |
| Open conversation, local history, unread notifications | Owner-client messenger presentation state. |

Schema in `metin2.db`:

```sql
messenger_friends(character_id, companion_id, added_unix)
messenger_messages(message_id, sender_id, recipient_id, body,
                   sent_unix, read_unix, delivered_unix)
```

Friend pairs are stored once per direction. Names/levels are resolved against player records. Presence, pending requests, and local unread state are not a durable replacement for the database.

Accepted messages are stored before routing. Offline non-friend recipients are refused; online recipients can receive a whisper without being friends. The coordinator and server resolve identity/ownership rather than trusting a client-selected persistent ID.

Coordinator handlers can call the mutex-serialized synchronous local backend. This is not a claim that every database query runs off the game thread; see [Persistence Architecture](../PersistenceArchitecture.md).

## Cluster flow

Map → coordinator messages include `messenger_login`, `messenger_request_add`, `messenger_answer`, `messenger_remove`, `messenger_send`, and `messenger_open`.

Coordinator → map pushes include snapshots, presence, requests, messages, sent-message echoes, conversation history, and results. The receiving map resolves the local PlayerState and ignores a player that has since traveled.

`ReplicatedMessengerId` mirrors the server-selected identity to the owner client. Sent-message echoes and received messages identify their companion using both endpoints, so an own echo is not counted as unread.

## Coordinator-free development

Without a connected coordinator, same-server whisper/friend-request fallback uses local components and character-name identity when no persistent identity exists. Accepted fallback friendships and messages are session-local, not stored in SQLite; offline/cross-map delivery is not available in that mode.

This permits focused PIE interaction but does not validate real cluster authentication, persistent identity, migration, or reconnect behavior.

## Current UI structure

The old messenger board was 170×300 with a title bar, collapsible companion groups, a lamp/name row, and five bottom commands. The current `UMT2MessengerWidget` default is also 170×300. Whispering uses the separate `UMT2WhisperWidget`, not an embedded conversation panel in a 500×400 messenger window.

The current messenger Blueprint requires these `BindWidget` controls:

| Name | Type |
| --- | --- |
| `RootSizeBox` | SizeBox |
| `TitleBarWidget` | MT2TitleBarWidget |
| `FriendsBox` | Panel |
| `AddFriendButton`, `WhisperButton`, `MobileButton`, `RemoveButton`, `GuildButton` | Button |

Missing required names/types are Blueprint integration errors, not silently optional controls. Add Friend opens a separate prompt. The Guild button opens guild UI; the messenger's Guild and Ignored groups still have no roster/ignore data source. Do not assume those empty groups mean the entire guild system is missing.

Dynamic rows are generated from component state. Single click selects; a second click within the configured double-click interval opens a whisper. Online/offline row art is configurable, with a texture-backed colored fallback. Headers collapse groups and empty groups retain an Empty row.

`MT2GameHUD` owns required messenger, whisper, friend-add, and related child widgets. Treat placement/dragging according to current HUD/Canvas ownership; the earlier instruction to create every window as an independent viewport widget is obsolete.

## Conversation and notification state

The whisper view reads the component's selected conversation. Closing clears the view selection, while local per-companion history supports reopening. Coordinator history can replace that local cache with the stored record.

Unread notifications are tracked independently of friend rows, so whispers from online non-friends can notify the player. Own echoes and messages in the currently open conversation do not increment that local unread count. The notification strip also reads active quest journal changes through delegates rather than continuous polling.

Companion GM presentation is resolved from checked authority where available; do not infer it from the local user's status or an unverified remote name.

## Generate the UI scaffold

```text
UnrealEditor-Cmd.exe <project.uproject> -run=MT2GenerateUIBlueprints -Messenger
```

The focused flag builds **messenger, whisper, notifications, friend-request, and friend-add** Blueprints. It does not mean "only the messenger asset." Generation writes assets and can rebuild widget trees; back up authored layouts and review submodule changes before running. Required HUD child bindings must still match the generated classes.

## Validation boundary

This source review did not run UI generation, test window placement, or exercise live cluster messaging, persistence/reconnect, and multiplayer travel. The current design must not be confused with full original-client visual or network parity.
