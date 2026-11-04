// utf8-text (java entry) -- repeat one 102-byte multilingual unit 4,096 times,
// encode the resulting text as UTF-8, then manually decode the 417,792 bytes
// for 32 rounds and fold the per-round counters into one integer.
//
// The non-ASCII characters are written as Unicode escapes so the source file is
// pure ASCII and compiles the same under any javac source encoding.
//
// Expected output: 78156718.

import java.nio.charset.StandardCharsets;

public class Main {
    private static final int REPEATS = 4096;
    private static final int ROUNDS = 32;
    private static final long MOD = 1_000_000_007L;

    private static final String UNIT =
            "ASCII-0123456789|\u65E5\u672C\u8A9E\u30C6\u30AD\u30B9\u30C8|"
            + "emoji-\uD83D\uDE00\uD83D\uDE80|caf\u00E9 na\u00EFve r\u00E9sum\u00E9|"
            + "\u03A9\u2248\u00E7\u221A\u222B\u02DC\u00B5\u2264\u2265\u00F7|";

    public static void main(String[] args) {
        StringBuilder builder = new StringBuilder(UNIT.length() * REPEATS);
        for (int r = 0; r < REPEATS; r++) {
            builder.append(UNIT);
        }
        String text = builder.toString();
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);

        long checksum = 0;
        for (int round = 0; round < ROUNDS; round++) {
            int i = 0;
            long codepoints = 0;
            long asciiCount = 0;
            long twoByte = 0;
            long threeByte = 0;
            long fourByte = 0;
            long codepointSum = 0;
            long codepointHash = 0;

            while (i < bytes.length) {
                int b0 = bytes[i] & 0xFF;
                int cp;
                int width;

                if (b0 < 0x80) {
                    cp = b0;
                    width = 1;
                    asciiCount = asciiCount + 1;
                } else if ((b0 & 0xE0) == 0xC0) {
                    cp = ((b0 & 0x1F) << 6) | (bytes[i + 1] & 0x3F);
                    width = 2;
                    twoByte = twoByte + 1;
                } else if ((b0 & 0xF0) == 0xE0) {
                    cp = ((b0 & 0x0F) << 12)
                            | ((bytes[i + 1] & 0x3F) << 6)
                            | (bytes[i + 2] & 0x3F);
                    width = 3;
                    threeByte = threeByte + 1;
                } else {
                    cp = ((b0 & 0x07) << 18)
                            | ((bytes[i + 1] & 0x3F) << 12)
                            | ((bytes[i + 2] & 0x3F) << 6)
                            | (bytes[i + 3] & 0x3F);
                    width = 4;
                    fourByte = fourByte + 1;
                }

                codepointSum = (codepointSum + cp) % MOD;
                codepointHash = (codepointHash * 131 + cp) % MOD;
                codepoints = codepoints + 1;
                i = i + width;
            }

            checksum = (checksum * 31
                    + codepoints
                    + asciiCount * 3
                    + twoByte * 5
                    + threeByte * 7
                    + fourByte * 11
                    + codepointSum
                    + codepointHash) % MOD;
        }

        System.out.println(checksum);
    }
}
