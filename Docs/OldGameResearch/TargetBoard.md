# Old Client: Target Board (uitarget.py)

Source studied: `D:\Giochi\Metin2\Development\Dumps\my_dump\uitarget.py`.

## Structure

- `TargetBoard(ui.ThinBoard)`: top-center thinboard (default 250x40, widens with name length:
  `200 + 7*len(name)`), containing the target name, a red HP gauge (130px, right-aligned,
  hidden until `SetHP` is first called - i.e. only for attackable/damaged targets) and a
  close button (`close_button_01/02/03.sub`).
- Below the board: a centered row of **small thin buttons**
  (`small_thin_button_01/02/03.sub` = Public.dds regions (114,202)-(174,222) normal,
  (174,202)-(234,222) hover, (0,232)-(60,252) down; 60x20 each), rebuilt per target.

## Buttons for a PLAYER target (RefreshButton / ShowDefaultButton)

Default set: **Whisper** (private message), **Exchange** (trade window), **Fight** (duel
request via `/pvp <vid>`), **Emotion allow**. Conditionally added:

- **Invite guild** (`SendGuildAddMemberPacket`) - only if the local player has guild
  authority `AUTH_ADD_MEMBER` and the target is guildless.
- **Friend** (`SendMessengerAddByVIDPacket`) - only if the target is not already a friend.
- Party logic: target in my party → hide Fight, show **Leave party** (if target is leader)
  or **Exclude** (if I am leader); target not in party and I am leader → **Invite party**;
  I am partyless but target is in a party → **Request enter party**.
- Special states: PVP instance/observer → name only; building → nothing; self →
  dismount/exit-observer menu only.

Mob/NPC targets never show the button row - just name (+ "Lv.X (grade)" prefix for
monsters) and the HP gauge for monsters.

## UE5 replication (this project)

`UMT2TargetInfoWidget` provides name, HP and close controls together with
a runtime-built row of small-thin-style buttons shown ONLY when the selected target is
another player: Message, Trade, Duel, Party, Friend, Guild. The HP bar is hidden for player
targets (old client only shows it once PVP damage flows). The buttons broadcast
`OnPlayerActionRequested` (action name + target actor). Whisper, friendship and trade use their corresponding HUD/controller operations. Duel uses the server-authoritative challenge/accept/revenge flow; see [Duels](../Duels.md). Other actions require the corresponding server authorization and UI integration.
