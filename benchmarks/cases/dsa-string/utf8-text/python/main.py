"""utf8-text (python entry) -- manually decode a 417,792-byte UTF-8 text (a
102-byte multilingual unit repeated 4,096 times) for 32 rounds, folding the
codepoint count and hash. One integer.

Run:   python main.py

Expected output: 78156718 (see the task README).
"""

UNIT = "ASCII-0123456789|日本語テキスト|emoji-😀🚀|café naïve résumé|Ω≈ç√∫˜µ≤≥÷|"
REPEATS = 4096
ROUNDS = 32
MOD = 1_000_000_007


def main():
    data = (UNIT * REPEATS).encode("utf-8")
    length = len(data)
    checksum = 0

    for _ in range(ROUNDS):
        i = 0
        codepoints = 0
        ascii_count = 0
        two_byte = 0
        three_byte = 0
        four_byte = 0
        codepoint_sum = 0
        codepoint_hash = 0

        while i < length:
            b0 = data[i]

            if b0 < 0x80:
                cp = b0
                width = 1
                ascii_count += 1
            elif (b0 & 0xE0) == 0xC0:
                cp = ((b0 & 0x1F) << 6) | (data[i + 1] & 0x3F)
                width = 2
                two_byte += 1
            elif (b0 & 0xF0) == 0xE0:
                cp = (
                    ((b0 & 0x0F) << 12)
                    | ((data[i + 1] & 0x3F) << 6)
                    | (data[i + 2] & 0x3F)
                )
                width = 3
                three_byte += 1
            else:
                cp = (
                    ((b0 & 0x07) << 18)
                    | ((data[i + 1] & 0x3F) << 12)
                    | ((data[i + 2] & 0x3F) << 6)
                    | (data[i + 3] & 0x3F)
                )
                width = 4
                four_byte += 1

            codepoint_sum = (codepoint_sum + cp) % MOD
            codepoint_hash = (codepoint_hash * 131 + cp) % MOD
            codepoints += 1
            i += width

        checksum = (
            checksum * 31
            + codepoints
            + ascii_count * 3
            + two_byte * 5
            + three_byte * 7
            + four_byte * 11
            + codepoint_sum
            + codepoint_hash
        ) % MOD

    print(checksum)


if __name__ == "__main__":
    main()
