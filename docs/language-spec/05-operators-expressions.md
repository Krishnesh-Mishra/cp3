# 5. Operators & expressions

Most operators match JS/TS exactly, with static typing removing the ambiguity JS has around them:

- *`==/===`   with static typing, a mismatched-type comparison between two *fixed* types wouldn't compile in the first place — but since union types are backed by `std::variant` ([§3](03-type-system.md)), a variable's actual runtime-held type can still genuinely vary, so the loose (`==`, coercing) vs. strict (`===`, exact type match) distinction stays meaningful.
- **`+` on mixed types** — not allowed directly (see [§3](03-type-system.md)'s mixed-type rules); requires an explicit conversion first, unlike JS's auto-coercing `+`.
- **Truthy/falsy in `if`** — full JS coercion behavior kept, for every type: `0`, `""`, `null`, `undefined`, `NaN` are falsy; non-empty strings, empty arrays/objects, etc. are truthy — exactly as in JS, not restricted to real `boolean` expressions only.
- **`?.` (optional chaining) / `??` (nullish coalescing)** — kept exactly as JS/TS has them.
- **Ternary (`? :`), bitwise operators, default parameter values** — identical to JS/TS, direct mapping, no changes.

