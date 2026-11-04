"""json-roundtrip (python entry) -- 20,000 iterations of json.loads +
json.dumps of one representative API record, folded into one integer.

Run:   python main.py

Same payload text, same field fold and same output as the cp3, JS and Go
entries. `separators=(",", ":")` keeps the compact form every other entry
emits, so the folded length of the stringified text is comparable.

Every length in the fold is a character count of ASCII text, so Python's
code-point count, V8's UTF-16 length and the byte counts used by Go and cp3
agree exactly.
"""

import json

TEXT = (
    '{"id":48271,"name":"Ada Lovelace","email":"ada.lovelace@example.com",'
    '"active":true,"score":93.5,"title":"Principal Engineer",'
    '"company":"Analytical Engines Ltd","city":"London","country":"GB",'
    '"tags":["analyst","engineer","pioneer","mathematician","compiler",'
    '"runtime"],"samples":[12,17,23,29,31,37,41,43,47,53]}'
)
ITERATIONS = 20000
MOD = 1000003


def main():
    checksum = 0
    for _ in range(ITERATIONS):
        u = json.loads(TEXT)
        out = json.dumps(
            {
                "id": u["id"],
                "name": u["name"],
                "email": u["email"],
                "active": u["active"],
                "score": u["score"],
                "title": u["title"],
                "company": u["company"],
                "city": u["city"],
                "country": u["country"],
                "tags": u["tags"],
                "samples": u["samples"],
            },
            separators=(",", ":"),
        )

        field_value = u["id"] + len(u["name"]) + len(u["email"])
        if u["active"]:
            field_value += 1
        field_value += int(u["score"] * 10) + len(u["title"]) + len(u["company"])
        field_value += len(u["city"]) + len(u["country"])
        field_value += len(u["tags"]) + len(u["samples"])
        for tag in u["tags"]:
            field_value += len(tag)
        for sample in u["samples"]:
            field_value += sample

        checksum = (checksum * 31 + field_value + len(TEXT) + len(out)) % MOD

    print(checksum)


if __name__ == "__main__":
    main()
