// buffer-copy (java entry) -- build a 64 KiB byte-pattern buffer, then reshape
// it ITERATIONS times: rotate it by a changing offset (two owning slices),
// reassemble it with the first byte overwritten by a cycling marker, and fold
// a 96-byte window into a checksum.
//
// The other five entries run the same reshape with their own buffer types
// (std::vector<uint8_t>, Vec<u8>, []byte, Node Buffer, bytearray). Every
// intermediate buffer is exactly SIZE bytes, and every entry folds the same
// two marker positions, so all six languages must print the same number.

import java.nio.charset.StandardCharsets;

public class Main {
    private static final int SIZE = 65536;
    private static final int ITERATIONS = 5000;
    private static final int WINDOW = 96;
    private static final String SEED =
            "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!?";
    private static final long MOD = 1000003L;

    private static int indexOf(byte[] data, byte wanted, int limit) {
        for (int i = 0; i < limit; i++) {
            if (data[i] == wanted) {
                return i;
            }
        }
        return -1;
    }

    private static long bufferCopyChecksum(int iterations) {
        byte[] seed = SEED.getBytes(StandardCharsets.US_ASCII);
        byte[] buf = new byte[SIZE];
        for (int i = 0; i < SIZE; i++) {
            buf[i] = seed[i % seed.length];
        }

        long checksum = 0;
        for (int k = 0; k < iterations; k++) {
            int offset = (k * 4093 + 17) % SIZE;
            byte marker = seed[k % seed.length];

            // Rotate left by offset and overwrite the new first byte with marker.
            byte[] next = new byte[SIZE];
            next[0] = marker;
            System.arraycopy(buf, offset + 1, next, 1, SIZE - offset - 1);
            System.arraycopy(buf, 0, next, 1 + SIZE - offset - 1, offset);
            buf = next;

            int firstA = indexOf(buf, (byte) 'a', WINDOW) + 1;
            int first7 = indexOf(buf, (byte) '7', WINDOW) + 1;
            checksum = (checksum * 131 + buf.length + firstA + first7) % MOD;
        }
        return checksum;
    }

    public static void main(String[] args) {
        System.out.println(bufferCopyChecksum(ITERATIONS));
    }
}
