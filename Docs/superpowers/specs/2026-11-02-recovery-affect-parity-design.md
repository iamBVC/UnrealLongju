# Recovery Affect Timing and Rounding Design

## Intent

Make queued HP/MP recovery match the legacy server's affect-event behavior while keeping recovery scheduling in the existing shared status-effect component.

## Verified current behavior

- The legacy `affect_event` calls `CHARACTER::UpdateAffect()` once per second. Queued HP recovery drains `min(pool, max HP * 9 / 100)` for the YMIR locale and `min(pool, max HP * 7 / 100)` otherwise. Queued SP recovery drains `min(pool, max SP * 7 / 100)`. These are integer expressions, so the percentage result truncates; there is no forced minimum chunk.
- Legacy continuous HP/SP recovery applies its fixed integer amount on the same one-second affect pass.
- Unreal already processes effects on a one-second server timer. Queued recovery currently defaults to 3% per second for both resources, calculates through floating point, and forces a minimum chunk of one. Thus its cadence is already one second, but its default drain amounts and small-value rounding differ from the legacy server.
- Unreal has no selected runtime locale setting. The existing per-resource rates are editable in Project Settings; use 7% as the standard default and configure HP to 9% for a YMIR deployment.
- The rate properties expose a 1–100% Project Settings range. Runtime also bounds config-loaded values to that range because editor metadata alone does not validate an INI override.
- The legacy client receives affect add/remove packets for display. Recovery and resource changes are server-authoritative.

## Goals and non-goals

- Match the legacy one-second recovery pass, HP locale rates, SP rate and integer truncation for queued recovery pools.
- Preserve the current shared effect timer, effect ownership, replication, save formats, and fixed-per-second potion behavior.
- Do not add a project-wide locale model or separate potion scheduler. Arena, dungeon, and guild-war accounting remain OWN-02S.
- Do not change Content assets or UI.

## Approaches considered

1. **Keep the shared one-second processor and correct its configured rates and integer drain calculation (recommended).** This matches the legacy event cadence, keeps one scheduler for status effects, and uses existing Project Settings for regional rate selection.
2. **Add a locale enum that selects 7% or 9% automatically.** This would establish a new locale system for a single setting when the project currently has no runtime locale model; defer until locale policy is designed across systems.
3. **Add potion-specific timers or retain the 3% defaults and document them.** Separate timers duplicate scheduling, while retaining 3% does not match the verified legacy server behavior.

## Acceptance criteria

- Standard defaults are HP 7% and MP 7% per one-second pool-recovery pass; the existing HP setting can be set to 9% for YMIR.
- Config-loaded rates outside the exposed 1–100% range are bounded before calculation, so a negative rate cannot keep an effect alive without progress.
- A pool tick uses integer point math equivalent to `min(pool, floor(max resource) * percent / 100)` with a wide intermediate, no forced minimum chunk, and no healing beyond the resource maximum.
- Queued HP/MP recovery still expires when its pool is spent or its resource is full. Continuous effects still apply their authored integer amount once per second and keep their existing duration/death behavior.
- Focused automation covers standard and YMIR HP rates, MP rate, non-divisible maxima, a pool smaller than a tick, resource caps, invalid negative config rate handling, and the zero-chunk boundary. Existing potion, status-effect, persistence and inventory regressions stay green.
- Archive and hash-check temporary tests, remove their exact source files and any verified-empty `Source/Metin2/Tests` directory, then pass Editor and Server Development Win64 builds.
- Update the item behavior documentation and the OWN-02D tracker entry with legacy source paths, test evidence, build results, and remaining integration limits.
