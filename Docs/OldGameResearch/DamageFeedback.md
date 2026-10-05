# Damage Feedback

Normal attack damage numbers are implemented. The display pipeline already accepts the future damage type.

Required behavior:

- Spawn at the hit actor's head and move upward with a small random horizontal direction.
- Animate opacity from 0 to 1, then from 1 to 0 before removal.
- Normal damage: yellow.
- Critical damage: red.
- Penetrating damage: purple.
- Poison damage: green.
- Damage amount and target come from the authoritative server result and are sent only to the attacking client.
- Critical, penetrating, and poison calculations still need to pass their type into this display pipeline.
