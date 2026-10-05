# NPCs and the quest/Lua system (old game)

Sources studied:
- `game/src/quest.h`, `questmanager/questlua*.cpp` (27 questlua_* API modules), `questevent.cpp`
- `game/src/quest/` (the `qc` quest compiler project)
- `share/locale/italy/quest/*.quest` (300 quest scripts; blacksmith.quest, buy_fishrod.quest read in full)
- `common/enums.h` (EOnClickEvents, mob rank/type), mob_proto NPC rows

## 1. NPCs are mob_proto rows

There is no separate NPC data source. An NPC is a `mob_proto` entry with:
- `bType = CHAR_TYPE_NPC` (also WARP, GOTO for teleporters; our importer already carries MobType),
- `bOnClickType`: `ON_CLICK_SHOP` (opens shop) or `ON_CLICK_TALK` (fires the quest CLICK/CHAT
  events), `ON_CLICK_NONE` for scenery mobs,
- AI effectively disabled (no aggro flags, battle type irrelevant), no exp/loot,
- same folder/model pipeline as monsters (our mob importer already creates their BPs and anims).

Clicking an NPC on the server routes to either the shop system or
`quest::CQuestManager::Click(player, npc)`.

## 2. The quest system

### Script format (.quest) and qc
Quests are written in a Lua dialect with structural sugar:

```
quest buy_fishrod begin
    state start begin
        when 9009.chat."Fishing rod" with pc.level>=7 begin
            say("...")
            local b = select("Yes", "No")
            if 1==b then pc.give_item2("27400", 1) setstate(notify_event) end
        end
    end
    state notify_event begin
        when letter begin makequestbutton("Fishing") q.start() end
        when button begin ... end
    end
end
```

The `qc` compiler turns `quest/state/when` blocks into plain Lua chunks; everything inside a
`when ... begin` body is ordinary Lua 5.0. Key concepts:
- **One state machine per quest per player.** Current state (+ arbitrary quest flags) persist in
  the DB (`quest` table: player id, quest name.flag, value). `setstate(x)` transitions;
  `__COMPLETE__` ends.
- **`when` clauses are event subscriptions** scoped to the current state, keyed by (event type,
  optional npc vnum) with an optional `with <condition>` guard evaluated before the body runs.
- **Event types** (quest.h): CLICK, KILL, PARTY_KILL, TIMER, SERVER_TIMER, LEVELUP, LOGIN, LOGOUT,
  BUTTON (quest button in the client letter UI), INFO, CHAT (the NPC dialog entry with a label),
  ATTR_IN/OUT (map trigger areas), ITEM_USE, ITEM_TAKE, ITEM_PICK, TARGET, ENTER/LEAVE_STATE,
  LETTER (state entered -> refresh quest letter UI), UNMOUNT, SIG_USE, ITEM_INFORMER.
- **`npcvnum.chat."Label"`** adds a dialog choice to that NPC's talk window; the body runs when
  the player picks it. Multiple quests contribute chat entries to the same NPC - the NPC dialog
  is assembled dynamically from every quest listening to that vnum.

### The scripting API (questlua_*.cpp)
27 modules register C functions into Lua: `pc.*` (level, money, give_item2, countitem, warp,
setqf/getqf quest flags...), `npc.*` (vnum, name, is_near...), `q.*` (start/done/set_title,
quest letter), `game.*`, `item.*`, `guild.*`, `party.*`, `building.*`, `horse.*`, `pet.*`, and
the dialog primitives.

### Dialog flow = suspended coroutines
`say(text)` accumulates dialog text; `select(a, b, ...)`/`wait()`/`input()` **suspend the Lua
coroutine**, send the accumulated script to the client (QuestScript packet), and resume with the
player's answer when the client replies. The client's `uiQuest.py` renders the text + buttons.
This suspend/resume model is the heart of the system - a quest body reads like a linear
conversation while actually being asynchronous.

### Quest letter
`q.start()` + `makequestbutton(title)` put an entry in the client's quest letter sidebar; the
BUTTON/INFO events fire when the player clicks it. `setskin(NOWINDOW)` suppresses the dialog
window for silent event handlers.

## 3. Shops

`ON_CLICK_SHOP` NPCs skip quests entirely: shop table (DB `shop`/`shop_item`) keyed by npc vnum,
fixed item lists, buy/sell using dwIBuyItemPrice/dwISellItemPrice (already on our templates).

## 4. Historical sizing

Historical dataset estimate: ~300 quest files per locale; most are small (a chat entry + a reward chain); a few (main story,
guild systems, events) are hundreds of lines. This is not the current importer count: the latest recorded audit parsed 238 scripts and refreshed 237 quest Blueprints; see [QuestPortingStatus](QuestPortingStatus.md). All content text lives inline (or via
locale string keys, e.g. `gameforge.blacksmith._30_say` in newer packs).
