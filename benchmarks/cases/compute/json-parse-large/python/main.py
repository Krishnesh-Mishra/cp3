"""json-parse-large (python entry) -- build a 204,472-byte compact JSON record
(50,000 numbers and 1,000 tags), parse it with json.loads for 20 iterations and
fold the parsed fields. One integer.

Run:   python main.py

Expected output: 613781 (see the task README).
"""

import json

NUMBERS = 50_000
TAGS = 1_000
ITERATIONS = 20
MOD = 1_000_003


def build_text():
    numbers = ",".join(str((i * 17 + 3) % 1000) for i in range(NUMBERS))
    tags = ",".join(f'"tag-{i}"' for i in range(TAGS))
    return (
        '{"id":48271,"name":"Ada Lovelace","active":true,"score":93.5,'
        '"numbers":[' + numbers + '],"tags":[' + tags + "]}"
    )


def main():
    text = build_text()
    text_length = len(text)
    checksum = 0

    for _ in range(ITERATIONS):
        payload = json.loads(text)
        numbers = payload["numbers"]
        tags = payload["tags"]

        field = payload["id"]
        field += len(payload["name"])
        if payload["active"]:
            field += 1
        field += int(payload["score"] * 10)
        field += len(numbers)
        field += len(tags)
        field += sum(numbers)
        field += sum(len(tag) for tag in tags)

        checksum = (checksum * 31 + field + text_length) % MOD

    print(checksum)


if __name__ == "__main__":
    main()
