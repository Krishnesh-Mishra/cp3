// env-injection (java entry) -- read one injected environment variable N times
// and fold its bytes into a checksum.
//
// The suite runner injects CP3_BENCH_ENV into every child process for this
// task. Each iteration performs a fresh System.getenv lookup (the lookup itself
// is the workload) and folds the value's length, byte sum, first byte and last
// byte into the checksum. A missing variable is an error, so a run without the
// injection fails instead of measuring an empty string.
//
// Same workload and same output as the C++, Rust, Go, JavaScript and Python
// entries.

import java.nio.charset.StandardCharsets;

public class Main {
    private static final int ITERATIONS = 500000;
    private static final String KEY = "CP3_BENCH_ENV";
    private static final long MOD = 1000003L;

    private static long envChecksum(int iterations) {
        long checksum = 0;
        for (int i = 0; i < iterations; i++) {
            String value = System.getenv(KEY);
            if (value == null || value.isEmpty()) {
                System.err.println("CP3_BENCH_ENV is not set");
                System.exit(1);
            }
            byte[] data = value.getBytes(StandardCharsets.UTF_8);
            long bytes = 0;
            for (byte b : data) {
                bytes = bytes + (b & 0xff);
            }
            checksum = (checksum * 31 + data.length) % MOD;
            checksum = (checksum * 31 + bytes) % MOD;
            checksum = (checksum * 31 + (data[0] & 0xff)) % MOD;
            checksum = (checksum * 31 + (data[data.length - 1] & 0xff)) % MOD;
        }
        return checksum;
    }

    public static void main(String[] args) {
        System.out.println(envChecksum(ITERATIONS));
    }
}
