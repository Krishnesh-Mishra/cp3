# utf8-text - UTF-8 decoding over multilingual text

Text workload that forces every language to operate on the same UTF-8 byte
sequence, including ASCII, accented Latin, Greek/math symbols, CJK and emoji.
Every language prints one integer.

## Workload

The source unit is exactly this string (the file is UTF-8):

```text
ASCII-0123456789|日本語テキスト|emoji-😀🚀|café naïve résumé|Ω≈ç√∫˜µ≤≥÷|
```

The unit is `102` UTF-8 bytes. Build the text by repeating the unit exactly
`REPEATS = 4096` times, then encode that text as UTF-8. The resulting byte
sequence is `417792` bytes. Do not operate on UTF-16 code units, Python code
points, or any other native string unit: every entry must decode the same
UTF-8 bytes.

Constants:

```text
REPEATS = 4096
ROUNDS  = 32
MOD     = 1000000007
```

For each of `ROUNDS` rounds, decode the byte sequence left to right:

```text
i = 0
codepoints = 0
asciiCount = 0
twoByte = 0
threeByte = 0
fourByte = 0
codepointSum = 0
codepointHash = 0

while i < byteLength:
    b0 = bytes[i]

    if b0 < 0x80:
        cp = b0
        width = 1
        asciiCount = asciiCount + 1
    else if (b0 & 0xE0) == 0xC0:
        cp = ((b0 & 0x1F) << 6) | (bytes[i + 1] & 0x3F)
        width = 2
        twoByte = twoByte + 1
    else if (b0 & 0xF0) == 0xE0:
        cp = ((b0 & 0x0F) << 12)
           | ((bytes[i + 1] & 0x3F) << 6)
           | (bytes[i + 2] & 0x3F)
        width = 3
        threeByte = threeByte + 1
    else:
        cp = ((b0 & 0x07) << 18)
           | ((bytes[i + 1] & 0x3F) << 12)
           | ((bytes[i + 2] & 0x3F) << 6)
           | (bytes[i + 3] & 0x3F)
        width = 4
        fourByte = fourByte + 1

    codepointSum = (codepointSum + cp) % MOD
    codepointHash = (codepointHash * 131 + cp) % MOD
    codepoints = codepoints + 1
    i = i + width
```

After decoding a round, fold the round into the checksum:

```text
checksum = (checksum * 31
          + codepoints
          + asciiCount * 3
          + twoByte * 5
          + threeByte * 7
          + fourByte * 11
          + codepointSum
          + codepointHash) % MOD
```

`checksum` starts at `0`. One line, one integer: `78156718`.

## Why this task

It measures the UTF-8 text representation and the cost of decoding a realistic
multilingual byte stream. The workload is intentionally byte-defined: it
compares UTF-8 storage and decode speed without letting a runtime's internal
string width change the amount of work. Emoji exercise 4-byte sequences, CJK
exercises 3-byte sequences, accented Latin and Greek/math symbols exercise
2-byte sequences, and ASCII exercises the 1-byte fast path.

## Language notes

- cpp: `std::string` bytes; read each byte through `unsigned char`.
- rust: `String` / `&str`, `as_bytes()`.
- go: `[]byte(text)`.
- js: `Buffer.from(text, "utf8")`; `buf[i]` is a byte.
- python: `text.encode("utf-8")`; `bytes` indexing yields an integer.
- java: `text.getBytes(StandardCharsets.UTF_8)`; mask each `byte` with
  `& 0xFF` before comparing or shifting.
- cp3: a cp3 string is a UTF-8 byte string; `TEXT.length` is the byte count
  and `TEXT[i]` returns one byte as a `number`. Index the string directly, no
  `Buffer` conversion is needed. Use `typeSystem: "rust"` in `package.json`
  for native `i64` counters.

No language may use a library UTF-8 decoder for the timed loop: the manual
decode above is the workload and keeps the operation count identical.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o utf8-text.exe main.cpp
./utf8-text.exe

# rust
rustc -C opt-level=3 -o utf8-text.exe main.rs
./utf8-text.exe

# go
go build -o utf8-text.exe main.go
./utf8-text.exe

# js
node main.mjs

# python
python main.py

# java
javac -d . Main.java
java Main

# cp3
build/bin/cp3c.exe build benchmarks/cases/utf8-text/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/utf8-text/cp3/src/index.cp3
```

The value is re-derived independently by `../../runner/oracle.py`
(`utf8_text_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the row with `python benchmarks/runner/run_benchmark.py --only utf8-text`.
