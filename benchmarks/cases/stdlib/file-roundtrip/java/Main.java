// file-roundtrip (java entry) -- write, read and stat one fixed 4 KiB file N
// times in the OS temp directory, then remove it.
//
// Same workload and same output as the C++, Rust, Go, JavaScript and Python
// entries. The checksum folds the bytes read (length, byte sum, first and last
// byte) and the stat result (size, isFile, isDirectory), so a language that
// wrote a different file or mis-stated the metadata would print a different
// number.

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.BasicFileAttributes;

public class Main {
    private static final int ITERATIONS = 200;
    private static final int FILE_BYTES = 4096;
    private static final String SEED =
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    private static final long MOD = 1000003L;

    private static byte[] payload() {
        byte[] seed = SEED.getBytes(StandardCharsets.US_ASCII);
        byte[] data = new byte[FILE_BYTES];
        for (int i = 0; i < FILE_BYTES; i++) {
            data[i] = seed[i % seed.length];
        }
        return data;
    }

    private static long roundtripChecksum(int iterations) throws IOException {
        byte[] data = payload();
        Path file = Paths.get(System.getProperty("java.io.tmpdir"),
                "cp3-bench-file-roundtrip.txt");

        long checksum = 0;
        for (int i = 0; i < iterations; i++) {
            Files.write(file, data);
            byte[] read = Files.readAllBytes(file);
            BasicFileAttributes stats = Files.readAttributes(file, BasicFileAttributes.class);

            long bytes = 0;
            for (byte b : read) {
                bytes = bytes + (b & 0xff);
            }

            checksum = (checksum * 31 + read.length) % MOD;
            checksum = (checksum * 31 + bytes) % MOD;
            checksum = (checksum * 31 + (read[0] & 0xff)) % MOD;
            checksum = (checksum * 31 + (read[read.length - 1] & 0xff)) % MOD;
            checksum = (checksum * 31 + stats.size()) % MOD;
            checksum = (checksum * 31 + (stats.isRegularFile() ? 1 : 0)) % MOD;
            checksum = (checksum * 31 + (stats.isDirectory() ? 1 : 0)) % MOD;
        }

        Files.delete(file);
        return checksum;
    }

    public static void main(String[] args) throws IOException {
        System.out.println(roundtripChecksum(ITERATIONS));
    }
}
