// sha256-churn (java entry) -- hash one fixed 1 KiB buffer ITERATIONS times
// and fold the first four digest bytes into a checksum.
//
// Same workload and same output as the cp3, Go, JavaScript and Python entries;
// the C++ and Rust entries are omitted because neither standard library ships
// SHA-256. java.security.MessageDigest is the standard-library hash surface.
//
// The buffer is the seed repeated to exactly 1024 bytes and never changes, so
// every hash is over identical bytes. The first four digest bytes are read as
// one big-endian unsigned 32-bit integer, matching parseInt of the first eight
// hex digits, binary.BigEndian.Uint32 and int.from_bytes.

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

public class Main {
    private static final int BUFFER_SIZE = 1024;
    private static final int ITERATIONS = 60000;
    private static final String SEED = "0123456789abcdef";
    private static final long MOD = 1000003L;

    private static long sha256Churn(int iterations) throws NoSuchAlgorithmException {
        byte[] seed = SEED.getBytes(StandardCharsets.US_ASCII);
        byte[] data = new byte[BUFFER_SIZE];
        for (int i = 0; i < BUFFER_SIZE; i++) {
            data[i] = seed[i % seed.length];
        }

        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long checksum = 0;
        for (int k = 0; k < iterations; k++) {
            byte[] hash = digest.digest(data);
            long firstFourBytes = ((long) (hash[0] & 0xff) << 24)
                    | ((long) (hash[1] & 0xff) << 16)
                    | ((long) (hash[2] & 0xff) << 8)
                    | (long) (hash[3] & 0xff);
            checksum = (checksum * 31 + firstFourBytes) % MOD;
        }
        return checksum;
    }

    public static void main(String[] args) throws NoSuchAlgorithmException {
        System.out.println(sha256Churn(ITERATIONS));
    }
}
