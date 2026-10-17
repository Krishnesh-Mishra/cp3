# Resolved at beta-one sign-off (nothing left open)

- **`Buffer` encodings - decided: `"utf8"` (the default), base64, and hex, and nothing more for beta one.** Those three cover the real early use cases (text, crypto digests, binary-over-JSON). No further encodings (`utf16le`, `latin1`, `ascii`, `base64url`, …) are added speculatively - adding one later is an additive change, not a decision that blocks anything.

Nothing else is flagged here for beta one.

