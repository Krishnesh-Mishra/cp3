// utf8-text (rust entry) -- repeat a 102-byte multilingual UTF-8 unit 4,096
// times (417,792 bytes), then hand-decode the same byte sequence 32 times,
// tracking codepoint counts, byte-width counts, a codepoint sum and a rolling
// hash.
//
// Build:  rustc -C opt-level=3 main.rs -o utf8-text.exe
// Run:    utf8-text.exe
//
// Expected output: 78156718

const UNIT: &str =
    "ASCII-0123456789|日本語テキスト|emoji-😀🚀|café naïve résumé|Ω≈ç√∫˜µ≤≥÷|";
const REPEATS: usize = 4096;
const ROUNDS: usize = 32;
const MOD: i64 = 1_000_000_007;

fn main() {
    let text = UNIT.repeat(REPEATS);
    let bytes = text.as_bytes();
    let len = bytes.len();

    let mut checksum: i64 = 0;
    for _ in 0..ROUNDS {
        let mut i: usize = 0;
        let mut codepoints: i64 = 0;
        let mut ascii_count: i64 = 0;
        let mut two_byte: i64 = 0;
        let mut three_byte: i64 = 0;
        let mut four_byte: i64 = 0;
        let mut codepoint_sum: i64 = 0;
        let mut codepoint_hash: i64 = 0;

        while i < len {
            let b0 = bytes[i];
            let cp: i64;
            let width: usize;

            if b0 < 0x80 {
                cp = b0 as i64;
                width = 1;
                ascii_count += 1;
            } else if (b0 & 0xE0) == 0xC0 {
                cp = (((b0 & 0x1F) as i64) << 6) | ((bytes[i + 1] & 0x3F) as i64);
                width = 2;
                two_byte += 1;
            } else if (b0 & 0xF0) == 0xE0 {
                cp = (((b0 & 0x0F) as i64) << 12)
                    | (((bytes[i + 1] & 0x3F) as i64) << 6)
                    | ((bytes[i + 2] & 0x3F) as i64);
                width = 3;
                three_byte += 1;
            } else {
                cp = (((b0 & 0x07) as i64) << 18)
                    | (((bytes[i + 1] & 0x3F) as i64) << 12)
                    | (((bytes[i + 2] & 0x3F) as i64) << 6)
                    | ((bytes[i + 3] & 0x3F) as i64);
                width = 4;
                four_byte += 1;
            }

            codepoint_sum = (codepoint_sum + cp) % MOD;
            codepoint_hash = (codepoint_hash * 131 + cp) % MOD;
            codepoints += 1;
            i += width;
        }

        checksum = (checksum * 31
            + codepoints
            + ascii_count * 3
            + two_byte * 5
            + three_byte * 7
            + four_byte * 11
            + codepoint_sum
            + codepoint_hash)
            % MOD;
    }

    println!("{}", checksum);
}
