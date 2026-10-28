"""date-format (python entry) -- construct UTC dates from one fixed epoch
millisecond value, format the components manually, and fold a fixed diff.

Run:   python main.py

Same workload, same fold and same output in every runnable language entry.
The formatted spelling is built by hand from UTC components, so every language
emits the same 19 ASCII characters: YYYY-MM-DDTHH:MM:SS.

`datetime.fromtimestamp(..., timezone.utc)` is the non-deprecated spelling of
`utcfromtimestamp` on current Python; the components are identical.
"""

import datetime

BASE_MS = 1700000000000       # 2023-11-14T22:13:20Z
LATER_MS = 1700000123456      # +123,456 ms
ITERATIONS = 50000
MOD = 1000003
HOUR_SECONDS = 3600

# '0'..'9' -> 0..9, '-' -> 10, 'T' -> 11, ':' -> 12. The same weights are used
# by every entry, so the character fold cannot drift between languages.
CHAR_VALUES = {str(digit): digit for digit in range(10)}
CHAR_VALUES.update({"-": 10, "T": 11, ":": 12})


def main():
    checksum = 0
    diff_ms = LATER_MS - BASE_MS

    for k in range(ITERATIONS):
        # Whole seconds only: every instant in this workload is second-aligned.
        instant_ms = BASE_MS + (k % HOUR_SECONDS) * 1000
        moment = datetime.datetime.fromtimestamp(
            instant_ms // 1000, datetime.timezone.utc
        )
        text = (
            f"{moment.year:04d}-{moment.month:02d}-{moment.day:02d}"
            f"T{moment.hour:02d}:{moment.minute:02d}:{moment.second:02d}"
        )
        char_sum = sum(CHAR_VALUES[ch] for ch in text)
        checksum = (checksum * 31 + char_sum + len(text) + diff_ms) % MOD

    print(checksum)


if __name__ == "__main__":
    main()
