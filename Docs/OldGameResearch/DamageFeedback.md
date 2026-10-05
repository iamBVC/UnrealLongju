# Damage Feedback

Source review: 2026-10-06.

`AMT2FloatingDamageActor` displays authoritative damage results on the attacking client. Its head-positioned text rises with horizontal variation and fades in/out. Client presentation does not determine gameplay damage.

Current `EMT2DamageDisplayType` colors:

| Type | Color |
| --- | --- |
| Normal | Yellow |
| Critical | Red |
| Penetrating | Blue |
| Critical + penetrating | Magenta |
| Poison | Green |

Critical/penetrating proc handling and display-type propagation already exist in `UMT2CombatComponent`; the former "future damage type" description was stale. A poison display enum is not proof of complete poison-affect/tick gameplay parity.

This review inspected code only; live visual/networked presentation was not tested.
