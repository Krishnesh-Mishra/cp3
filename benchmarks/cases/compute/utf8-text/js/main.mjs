// utf8-text (js entry) -- encode a 102-byte multilingual unit repeated 4,096
// times, then manually decode the 417,792 UTF-8 bytes for 32 rounds and fold
// the per-round counts into one checksum.
//
// Run: node main.mjs
//
// Expected output: 78156718

const UNIT =
  "ASCII-0123456789|日本語テキスト|emoji-😀🚀|café naïve résumé|Ω≈ç√∫˜µ≤≥÷|";
const REPEATS = 4096;
const ROUNDS = 32;
const MOD = 1000000007;

const TEXT = UNIT.repeat(REPEATS);
const bytes = Buffer.from(TEXT, "utf8");
const byteLength = bytes.length;

let checksum = 0;
for (let round = 0; round < ROUNDS; round++) {
  let i = 0;
  let codepoints = 0;
  let asciiCount = 0;
  let twoByte = 0;
  let threeByte = 0;
  let fourByte = 0;
  let codepointSum = 0;
  let codepointHash = 0;

  while (i < byteLength) {
    const b0 = bytes[i];
    let cp;
    let width;

    if (b0 < 0x80) {
      cp = b0;
      width = 1;
      asciiCount++;
    } else if ((b0 & 0xe0) === 0xc0) {
      cp = ((b0 & 0x1f) << 6) | (bytes[i + 1] & 0x3f);
      width = 2;
      twoByte++;
    } else if ((b0 & 0xf0) === 0xe0) {
      cp =
        ((b0 & 0x0f) << 12) |
        ((bytes[i + 1] & 0x3f) << 6) |
        (bytes[i + 2] & 0x3f);
      width = 3;
      threeByte++;
    } else {
      cp =
        ((b0 & 0x07) << 18) |
        ((bytes[i + 1] & 0x3f) << 12) |
        ((bytes[i + 2] & 0x3f) << 6) |
        (bytes[i + 3] & 0x3f);
      width = 4;
      fourByte++;
    }

    codepointSum = (codepointSum + cp) % MOD;
    codepointHash = (codepointHash * 131 + cp) % MOD;
    codepoints++;
    i += width;
  }

  checksum =
    (checksum * 31 +
      codepoints +
      asciiCount * 3 +
      twoByte * 5 +
      threeByte * 7 +
      fourByte * 11 +
      codepointSum +
      codepointHash) %
    MOD;
}

console.log(checksum);
