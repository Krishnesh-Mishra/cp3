// file-big (java entry) -- write and read an 8 MiB deterministic payload eight
// times through a temp file, folding length, byte sum, first byte and last
// byte each iteration. Bytes are masked with & 0xFF before folding.
//
// Expected output: 779913.

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

public class Main {
    private static final int SIZE = 8_388_608;
    private static final int ITERATIONS = 8;
    private static final long MOD = 1_000_003L;
    private static final String SEED =
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    public static void main(String[] args) throws IOException {
        byte[] seed = SEED.getBytes(StandardCharsets.US_ASCII);
        byte[] payload = new byte[SIZE];
        for (int offset = 0; offset < SIZE; offset += seed.length) {
            System.arraycopy(seed, 0, payload, offset, seed.length);
        }

        Path path = Paths.get(System.getProperty("java.io.tmpdir"),
                "cp3-bench-file-big-java.bin");

        long checksum = 0;
        try {
            for (int iteration = 0; iteration < ITERATIONS; iteration++) {
                Files.write(path, payload);
                byte[] read = Files.readAllBytes(path);

                long byteSum = 0;
                for (byte b : read) {
                    byteSum += b & 0xFF;
                }

                checksum = (checksum * 31 + read.length) % MOD;
                checksum = (checksum * 31 + byteSum) % MOD;
                checksum = (checksum * 31 + (read[0] & 0xFF)) % MOD;
                checksum = (checksum * 31 + (read[read.length - 1] & 0xFF)) % MOD;
            }
        } finally {
            Files.deleteIfExists(path);
        }

        System.out.println(checksum);
    }
}
