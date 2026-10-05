# Messenger (friends, presence, private messages)

## What the old game did

`game/src/messenger_manager.{h,cpp}` — a singleton holding the friend graph in memory, backed by one
SQL table:

```sql
messenger_list(account, companion)
```

- **Friendship is symmetric and consensual.** `RequestToAdd` (line 115) sends the target a
  `messenger_auth <name>` command packet; `AuthToAdd` (line 141) checks the request actually exists
  (via a CRC of the two names) and then calls `AddToList` **both ways**. A name alone never puts
  someone on your list.
- **Removal is symmetric too** (`RemoveFromList`, line 224 deletes the row; `RemoveAllList` clears a
  character entirely) — a one-sided friendship would leave the other player with a companion who
  never lights up.
- **Presence is pushed, not polled.** `Login`/`Logout` send `SendLogin`/`SendLogout` to every
  companion, and `P2PLogin`/`P2PLogout` carry the same across game servers.
- **Whispers were live-only.** There is no offline storage anywhere in the original: a whisper to a
  player who is not online is simply refused.

## What this project does

Same model, with one deliberate extension: **private messages are persisted**, so a message to an
offline friend is waiting at their next login on any server. That is a requested addition, not old-game
behaviour.

### Where the pieces live

The cluster already has a **coordinator** process that owns the SQLite database and routes JSON
between map servers (`UMT2ServerRuntimeSubsystem`), with `CharacterLeases` tracking who is online and
on which map instance. The messenger is built on exactly that:

| concern | owner | why |
|---------|-------|-----|
| friend graph, stored messages | coordinator (SQLite) | one source of truth for the whole cluster |
| presence | coordinator (`MessengerPresences`) | session state; re-learned as servers reconnect |
| pending friend requests | coordinator, in memory | matches `m_set_requestToAdd`; does not survive a restart |
| player-facing state | `UMT2MessengerComponent` on PlayerState | replicated `COND_OwnerOnly` — a friend list is private |

Schema (mirrors `messenger_list`, keyed by persistent character id so a rename does not break a
friendship):

```sql
messenger_friends(character_id, companion_id, added_unix)      -- one row per direction
messenger_messages(message_id, sender_id, recipient_id, body,
                   sent_unix, read_unix, delivered_unix)
```

Names and levels are **joined from `players`** rather than copied, so a rename or a level-up shows up
without a migration.

### Message flow

Map server → coordinator: `messenger_login`, `messenger_request_add`, `messenger_answer`,
`messenger_remove`, `messenger_send`, `messenger_open`.
Coordinator → map server: `messenger_snapshot`, `messenger_presence`, `messenger_request`,
`messenger_message`, `messenger_message_sent`, `messenger_conversation`, `messenger_result`.

Each push names the character it is for; the receiving map server resolves it to a local PlayerState
(the same walk the party snapshot uses) and drops it if that player has since travelled.

A sent message is **stored before it is delivered**: one that the recipient never sees because their
server dropped in between is still in the store for their next login, which is the whole point of
persisting them. `delivered_unix` records the first time it reached a client, which is what
distinguishes "you were offline for this" from a live message.

Only friends may message each other, so the friend list doubles as the block list.

### Notes

- `UMT2PersistenceManager::GetLocalBackend()` gives the coordinator synchronous access to the
  database. The async API exists because *map servers* reach the database over the network; code
  running inside the coordinator is already next to the file, and these are small indexed lookups.
- Presence drops in `HandleCharacterLogout` **before** the lease is erased, so companions get the
  logout with the name still attached.

## The old window (uimessenger.py / UIScript/MessengerWindow.py)

`MessengerWindow.py` declares a small floating board, **170x300**, `("movable", "float")`, of type
`board_with_titlebar` — not a full-screen panel. Its children are just a scrollbar and a row of icon
buttons pinned to the bottom centre, 30px apart (`BUTTON_START_X_POS = -60`, `BUTTON_X_STEP = 30`):

| button | image |
|---|---|
| AddFriendButton | `messenger_add_friend_01..04.sub` |
| WhisperButton | `messenger_whisper_01..04.sub` |
| MobileButton | `messenger_mobile_01..04.sub` |
| RemoveButton | `messenger_delete_01..04.sub` |
| GuildButton | `messenger_guild_01..04.sub` |

The list itself is built in code, not in the script. `MessengerItem` (uimessenger.py line 18) is one
row: an ImageBox lamp at x=0 and a TextLine at **x=20, y=2**, sized `20 + 6*len(name) + 4` by **16**.
`MessengerMemberItem` swaps the lamp between three states — `messenger_list_online.sub`,
`messenger_list_offline.sub`, `messenger_list_mobile.sub`. Rows sit under collapsible groups
(`MessengerFriendGroup`, `MessengerGuildGroup`, `MessengerFamilyGroup`), each indenting its members by
`GetStepWidth()` = 15.

Interaction: **single click selects** (`OnMouseLeftButtonDown` -> `OnSelectItem`), **double click
whispers** (`OnMouseLeftButtonDoubleClick` -> `OnDoubleClickItem`), and the bottom buttons act on the
current selection. A selected row draws a blue bar behind itself in `OnRender`:
`grp.GenerateColor(0.0, 0.0, 0.7, 0.7)`.

Whispering opened a **separate** window (uiwhisper.py): the companion's name in the title bar, a chat
log, an input line and a send button.

### How this maps here

`UMT2MessengerWidget` keeps the same behaviour but carries both panels, so one Blueprint covers the
list and the whisper log. Everything visual is a `BindWidgetOptional` property, so the layout, the
board art and the button icons are authored in `WBP /Game/UI/MT2Messenger` rather than in C++.
Bindings are optional on purpose: a Blueprint that is still being built compiles and runs, and the
widget logs once which names it did not find.

Expected widget names:

| name | type | purpose |
|---|---|---|
| `RootSizeBox` | SizeBox | sized from `WindowSize` (default 500x400) |
| `TitleText` | TextBlock | window title |
| `FriendsBox` | any panel | friend rows are generated into it |
| `RequestsBox` | any panel | pending friend requests |
| `StatusText` | TextBlock | refusal messages |
| `AddNameBox` | EditableTextBox | name to add |
| `AddFriendButton` / `WhisperButton` / `RemoveButton` | Button | the old bottom button row |
| `ConversationPanel` | any widget | shown/hidden as a unit |
| `ConversationTitle` | TextBlock | companion's name |
| `ConversationBox` | ScrollBox | the whisper log |
| `MessageBox` | EditableTextBox | input line |
| `SendButton` / `CloseConversationButton` | Button | send, and back to the list |

Selection follows the original: the first click selects a row (drawn with `SelectionColor`, defaulting
to the old blue bar), a second click on the same row opens the whisper, and the Whisper/Remove buttons
act on the selection. Rows are still generated in C++ because their number is dynamic; their colours
and font size are exposed as properties.

### Generating the window

`WBP /Game/UI/MT2Messenger` is produced by the project's own UI generator:

```
UnrealEditor-Cmd UnrealLongju.uproject -run=MT2GenerateUIBlueprints -Messenger
```

The `-Messenger` flag builds only this window, so every other UI asset is left alone. The layout is
absolute-positioned on a canvas inside a `RootSizeBox` (500x400), which is what keeps the window from
stretching to the viewport, and uses the same thinboard nine-slice frame and `MT2TitleBar` child as the
game's other floating windows.

All fifteen bindings are **required** `BindWidget`s, so the Blueprint's compile fails if one is renamed
or deleted. Re-running the generator rebuilds the tree from scratch — any hand fine-tuning in the
editor is lost, so it is a scaffold to start from, not something to re-run afterwards.

## The whisper dialog (uiscript/whisperdialog.py)

Whispering is its **own window**, not part of the messenger list — the messenger's Whisper button and
the target board's "Message" button both open it. `whisperdialog.py` declares a **280x200** `thinboard`,
`("movable", "float")`:

| child | position | notes |
|---|---|---|
| `name_slot` + `titlename` | (10,10), text at (3,3) | the companion's name |
| `gamemastermark` | (206,6) | `ymirred.tga`, GM only |
| `minimizebutton` | (280-41, 12) | |
| `closebutton` | (280-24, 12) | |
| `scrollbar` | (280-25, 35), size 120 | the log's scrollbar |
| `editbar` | (10, height-60), 262x50, `0x77000000` | holds the two below |
| `chatline` | (5,5) inside the bar, multi_line, input_limit 40 | |
| `sendbutton` | (280-80, 10) inside the bar | |

`UMT2WhisperWidget` mirrors this. It owns no state: which conversation it shows comes from the
messenger component's open conversation, so the list window and the dialog can never disagree. Closing
it clears that conversation; minimising collapses everything but the title row, as the original did.

Enter sends. The old `chatline` was a multi-line editline, and a UMG `UMultiLineEditableTextBox`
swallows the Enter key rather than firing a commit, so the newline it leaves behind is the signal — it
is stripped before the message is sent.

`MT2Messenger` is therefore the list only, and is back to roughly the original's proportions (240x400
rather than 170x300, to fit the map and channel on a row). Both Blueprints come from the same
`-Messenger` generator run.

## Identity outside a real cluster (PIE, standalone)

`AMT2GameModeBase::PreLogin`/admission is gated on `Runtime->IsMapServer()`, so **PIE never runs it** —
which means `SetPersistenceIdentity` is never called and the persistent character id is empty. Anything
that keys off that id silently does nothing there. (The quest system hit the same trap: entry events
were once placed in the admission path and never fired in PIE.)

The messenger therefore resolves identity through `GetMessengerIdFor`: the persistent character id when
there is one, otherwise the **character name**. Names are unique in the `players` table, so the two
identities cannot collide for a real session.

Delivery follows the same split. With a coordinator, a message goes up to it and is stored and routed.
Without one — PIE, or a single map server running alone — the server delivers it directly to the target
player's component on the same server and echoes it to the sender. Nothing is stored in that mode, so
it only reaches someone who is here and online, which is exactly what the old game's live-only whisper
did anyway.

Presence announcements are skipped entirely without a coordinator: presence is its state to track, and
a lone server has nobody to tell.

## Notification strip

`UMT2NotificationsWidget` (`WBP /Game/UI/MT2Notifications`) is a standing HUD element, not a window:
one entry per active quest and one per companion with unread whispers, each an icon with its name
underneath. Quests use `/Game/icon/item/T_scroll_open`, messages `/Game/icon/action/T_letter`; both are
`EditAnywhere` so they can be swapped without code.

It reads from the two components that already publish change delegates — the quest manager's journal
(`OnJournalChanged`) and the messenger (`OnMessengerChanged`) — so it never polls. Clicking a quest
opens that quest's dialog, exactly as clicking its row in the quest log does; clicking a message opens
the whisper dialog on that companion. With nothing pending the strip collapses rather than sitting
empty.

Entries are generated into a bound `NotificationsBox`; it is a `UWrapBox` in the generated Blueprint so
a long list wraps instead of running off the screen edge.

## A message you sent was invisible

`ClientReceiveMessage` added a line to the open conversation only when
`OpenConversationId == Message.SenderCharacterId`. That holds for a message *received* — the sender is
the companion — but the echo of the player's own message names **them** as the sender and the companion
as the recipient, so every line they typed was dropped. A line now belongs to the open conversation
when either end of it is the open companion.

## The GM mark belongs to the companion

`interfacemodule.py` line 1574 calls `dlg.SetGameMasterLook()` under `IsGameMasterName(name)`, where
`name` is the whisper partner — so the mark says "the person you are talking to is a GM", not "you
are". The flag is resolved server-side from the companion's `AMT2PlayerState::IsAdmin()` and sent to
the client with the conversation, for both entry points (target board and friend list). A companion on
another server leaves it hidden: presence does not carry the flag yet, and the mark should not claim
something that was not checked.

## Unread notifications are not the friend list

The notification strip first counted unread messages from `FMT2FriendEntry::UnreadCount`, so a whisper
from someone who is **not a friend** produced no notification at all — and whispering is open to any
online player, so that is the common case.

Unread state is now tracked on the client, in the messenger component: a map of companion id to count
plus the sender's name, filled by `ClientReceiveMessage` and cleared when that conversation is opened.
Two rules fall out of doing it there rather than on the server:

- **A message whose conversation is already open never counts.** The client is the only side that
  knows which whisper window the player is looking at (`OpenConversationId` is set by the client when
  it opens one), so this test can only be made there.
- **The player's own echo never counts**, since the sender is themselves.

The friend list's own `UnreadCount` still comes from the coordinator and drives the badge on a
companion's row; the client map drives the notification strip.

## A conversation opened after the fact was blank

Messages were only ever added to `Conversation`, which exists solely for the window that is open. A
whisper that arrived while the window was closed raised its notification and was then **thrown away** —
so clicking that notification opened an empty log. The coordinator's stored history filled the gap in a
real cluster, but a server running without one stores nothing, so there was nothing to fall back on.

The client now keeps its own `ConversationHistory`, a per-companion list built from every message it
sees in either direction. Opening a conversation seeds the log from it; when the coordinator's stored
copy arrives it replaces that entry, being the complete one.

### The client cannot work out its own id

`GetCompanionOf` has to know which end of a message is the local player. On a client the persistent
character id is not available — `UMT2PersistenceComponent` is server-only — so `GetMessengerIdFor`
would fall back to the character name and disagree with the server's persistent id in a real cluster.
The client would then read its own messages as someone else's: wrong colour in the log, and an unread
notification for its own echo.

The server therefore mirrors its id down in a `COND_OwnerOnly` `ReplicatedMessengerId`, set as soon as
the character record is restored, and the client uses that. In PIE the two agree anyway (both fall back
to the name), which is exactly why the bug would not have shown up until a real cluster ran.

### …and the log was still empty

The history fix above was correct but dead: `OpenConversation` seeded `Conversation` from
`ConversationHistory` and then, two lines later, a leftover `Conversation.Reset()` wiped it. The reset
belonged to the original version of the function, before the seeding existed. `CloseConversation` still
resets — that clears the *view* while the history keeps the record for reopening.

## Dragging the whisper window

`whisperdialog.py`'s `window["style"]` is `("movable", "float")`, so the dialog could be dragged by its
title bar. `UMT2WhisperWidget` does the same over the top `TitleBarHeight` (34px by default) of the
window: press captures the mouse, move repositions, release lets go.

It cannot reuse the shop window's idiom, which drags a `UCanvasPanelSlot` inside its own tree — the
whisper dialog is added straight to the viewport and has no such slot. It tracks its position itself
and calls `SetPositionInViewport`, converting the screen-space delta by the geometry scale (screen space
is device pixels, viewport position is not). The position is clamped so the title bar can never leave
the screen, since a window dragged past the edge could never be grabbed back.

## Friend requests from the target board

The "Friend" button asks that player directly, mirroring the old `RequestToAdd` ->
`messenger_auth <name>` -> `AuthToAdd` handshake: the target answers a prompt
("{Name} sent you a friend request", accept / deny) and only then is the pair written, both ways.

The character id is server-only, so — like whispering — the actor travels to the server and is resolved
there. The server also checks the cheap refusals before anything is sent (self, already friends, list
full) so the asker gets an immediate answer.

`UMT2FriendRequestDialogWidget` (`WBP /Game/UI/MT2FriendRequestDialog`) is the prompt, built on the
same small centred board as the party invite since it asks the same kind of question. Its root canvas
is `SelfHitTestInvisible` so only the dialog takes the mouse.

### Without a coordinator

The coordinator owns the friend graph, so with one connected the request and the answer both go
through it and the pair lands in `messenger_friends`. Without one — PIE, or a lone map server — the
request is delivered straight to the target's component (they are on this server by definition, since
the request came from clicking them in the world) and accepting adds the entry to both sides in memory.

**That friendship is session-only**: there is no database to write it to, so it is gone at logout. It
is enough to exercise the whole flow in PIE, but a persistent friend list needs the coordinator running.

## The taskbar button

`UMT2TaskbarWidget` has always had a `MessengerButton` that broadcasts `OnMessengerClicked` — but
nothing subscribed to it, so the button did nothing. `UMT2GameHUDWidget` now binds it (next to the
inventory button it already handled) and forwards to `AMT2HUD::ToggleMessengerWindow`, since the
window belongs to the HUD actor rather than to the HUD widget.

## Friend rows

The old row was a 16px lamp at x=0 with the name at x=20 (uimessenger.py `MessengerItem`), the lamp
swapping between `messenger_list_online.sub` and `messenger_list_offline.sub`. Those images are not
extracted into the project yet, and their atlas coordinates are not in `atlasinfo.txt`, so the row
draws a solid dot in the online/offline colour instead. `OnlineIcon` / `OfflineIcon` are exposed on the
widget: assigning either replaces the dot with the real art, no code change.

Interaction follows the original exactly: **single click selects** (`OnMouseLeftButtonDown` ->
`OnSelectItem`), **double click whispers** (`OnMouseLeftButtonDoubleClick` -> `OnDoubleClickItem`), and
the bottom buttons act on the selection. UMG buttons raise no double-click event, so a second click on
the same row within `DoubleClickSeconds` (0.35 by default) counts as one.

## Groups

The list is not flat. `MessengerGroupItem` gives each group a collapsible header whose chevron comes
from `messenger_list_open.sub` / `messenger_list_close.sub`, and members are indented by
`GetStepWidth()` = 15. Inside a group the original lists `GetLoginMemberList()` before
`GetLogoutMemberList()`, so whoever you can actually talk to is at the top - the rows are sorted the
same way here, then by name.

The three groups are **Friends**, **Guild** and **Ignored** (`MESSENGER_FRIEND` / `MESSENGER_GUILD`;
the GF build shows Ignored where the older client had `MESSENGER_FAMILY`). An empty group still shows
its header with `MESSENGER_EMPTY_LIST` = "Empty" underneath rather than disappearing. Guild and Ignored
are always empty for now: there is no guild roster in the messenger and no ignore list yet, so they are
structure without a source.

The window is back to roughly the original's proportions - 210x340 against `messengerwindow.py`'s
170x300, wider only so a name and its unread badge fit, and taller for the add-a-friend line the old
client kept in a separate dialog. The bottom row carries three of the original's five buttons; mobile
and guild have nothing behind them here.

## Viewport widgets steal the mouse unless pinned

`AddToViewport` gives a widget a slot that **fills the screen**, and a filled slot stretches whatever is
inside it. A window rooted in a `USizeBox` therefore looks the right size — the size box constrains its
child — while the widget itself covers the entire viewport and takes every click: camera, movement and
the other windows all stop responding.

Two things fix it, and both are needed:

- `SetDesiredSizeInViewport(WindowSize)` plus `SetAlignmentInViewport(0,0)` in `NativeConstruct`, so the
  slot is pinned to the window's own size rather than filling. This also matters for the whisper
  dialog's title-bar drag: with a stretched slot the local coordinates are measured from the screen
  corner, not the window's.
- The root `UCanvasPanel` inside is `SelfHitTestInvisible`, so the window's empty corners do not take
  clicks either — only the board backdrop and the controls on it do.

The notification strip has no board at all, so it goes further: the widget itself is never `Visible`,
only `SelfHitTestInvisible`, and just its entry buttons take the mouse.

## Three things a generated window does not get for free

- **The title bar's X.** `UMT2TitleBarWidget` raises `OnCloseClicked`, but a window that never subscribes
  to it has a dead close button. The messenger binds it to hide itself.
- **Dragging.** `messengerwindow.py`'s style is `("movable", "float")` like the whisper dialog's, so the
  board drags by its title band (`TitleBarHeight`, 30px). Same mechanics as the whisper window: a
  viewport widget has no slot to read a position back from, so it tracks its own and calls
  `SetPositionInViewport`, clamped so the title bar cannot leave the screen. The first drag reads the
  window's current on-screen position out of the geometry, so it does not jump.
- **A brush with no resource draws nothing.** The friend-row lamp set only a size and a colour on its
  `UImage`, which is invisible - a tint needs something to tint. It now tints
  `/Engine/EngineResources/WhiteSquareTexture` when no lamp art is assigned, which gives the solid dot
  the colour was always meant to produce.
