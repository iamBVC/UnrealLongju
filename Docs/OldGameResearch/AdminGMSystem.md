# Admin / GM system (old game)

Sources studied:
- `server_src/game/src/gm.cpp` (GM list handling)
- `server_src/common/enums.h` (EGMLevels)
- `client_src/source/UserInterface/InstanceBaseEffect.cpp` (GM mark effect)
- `my_dump/playersettingmodule.py`, `my_dump/localeinfo.py` (effect registration)
- `my_dump/locale/en/effect/gm.mse` + `ymirred.tga` (the effect itself)

## Server side

- GM authority levels (`EGMLevels`): `GM_PLAYER`, `GM_LOW_WIZARD`, `GM_WIZARD`,
  `GM_HIGH_WIZARD`, `GM_GOD`, `GM_IMPLEMENTOR`.
- The GM list lives in a separate database table (`gmlist`: account, player name,
  contact IP, server IP, authority). `gm.cpp` caches it in `g_map_GM`, **keyed by
  character name**, with an optional allowed-host set (`gmhost`).
- `gm_new_get_level(name, host, account)` resolves a player's level; the very first
  check is `if (test_server) return GM_IMPLEMENTOR;` — on test servers **everyone
  is a full GM**. This maps 1:1 to our "PIE players are admins by default" rule.
- Chat commands are dispatched through an interpreter table where each command
  declares a minimum GM level; normal players only get the `GM_PLAYER` commands.

## Client side (GM mark)

- The GM mark is affect slot 0: `chrmgr.RegisterEffect(chrmgr.EFFECT_AFFECT+0,
  "Bip01", localeInfo.FN_GM_MARK)` in `playersettingmodule.py`.
- `FN_GM_MARK = "<locale>/effect/gm.mse"` — a locale asset, not a `ymir work` one.
- `gm.mse` is a looping particle emitter attached to the root bone "Bip01":
  bounding sphere at Z=120, emission point Z≈110 (just above the head), one
  billboarded 64x64 particle per 0.5s cycle rising slowly (~15 uu/s upward
  emission), lifetime ≈2.2s, additive-style blending (Src 5 / Dest 6).
- The particle texture is `locale/en/effect/ymirred.tga` — the red "YMIR" logo
  (already imported in the UE project as `/Game/locale/en/effect/T_ymirred`).
- Extra GM behavior in `InstanceBaseEffect.cpp`: GMs never receive the enemy-empire
  marker (`__AttachEmpireEffect` early-outs for GM instances) and the YMIR affect is
  suppressed while invisible (`AFFECT_INVISIBILITY`).

## UE recreation decisions

- Admin list: `admins` SQLite shard via `UMT2PersistenceManager` (entity type
  `admin`, routed to the admins DB by `RouteEntityType`). Entity id = character
  name lowercased, matching the old game's name-keyed GM map.
- PIE sessions grant admin to everyone, replicating `test_server` behavior.
- Only admins may execute `/` chat commands (server-side gate in
  `ServerExecuteChatCommand`).
- GM mark: screen-space widget component above the head showing `T_ymirred`
  (stand-in for the gm.mse particle loop until a particle recreation exists).
