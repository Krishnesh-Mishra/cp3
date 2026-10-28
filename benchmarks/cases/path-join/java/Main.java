// path-join (java entry) -- join, normalize, basename and extname one fixed
// path-segment list N times and fold stable string lengths and characters.
//
// Same workload and same output as the C++, Rust, Go, JavaScript and Python
// entries. Only lengths and the first character of each result are folded, so
// the checksum is identical on Windows and POSIX (the separator is one
// character in every language's path surface).

import java.nio.file.Path;
import java.nio.file.Paths;

public class Main {
    private static final int ITERATIONS = 170000;
    private static final long MOD = 1000003L;

    private static long pathChecksum(int iterations) {
        long checksum = 0;
        for (int i = 0; i < iterations; i++) {
            Path joinedPath = Paths.get("var", "log", "cp3", "service",
                    "events-2026-10-23.log");
            String joined = joinedPath.toString();
            String normalized = joinedPath.normalize().toString();
            String base = joinedPath.getFileName().toString();

            int dot = base.lastIndexOf('.');
            String ext = dot <= 0 ? "" : base.substring(dot);

            checksum = (checksum * 31 + joined.length()) % MOD;
            checksum = (checksum * 31 + normalized.length()) % MOD;
            checksum = (checksum * 31 + base.length()) % MOD;
            checksum = (checksum * 31 + ext.length()) % MOD;
            checksum = (checksum * 31 + joined.charAt(0)) % MOD;
            checksum = (checksum * 31 + base.charAt(0)) % MOD;
            checksum = (checksum * 31 + ext.charAt(0)) % MOD;
        }
        return checksum;
    }

    public static void main(String[] args) {
        System.out.println(pathChecksum(ITERATIONS));
    }
}
