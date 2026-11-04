// string-churn (java entry) -- build a 4 KiB string, then repeatedly rebuild
// strings from it: uppercase, slice, lowercase + re-concatenate, re-slice.
//
// Run:    java Main [iterations]        (default 50000)
//
// Same workload and same output in every language entry. Every round allocates
// new strings, which is what makes this task exercise allocators and GCs. The
// per-iteration fold (checksum * 31 + first 'c' position + length) depends on
// the exact string contents, so all six languages must print the same number.

import java.util.Locale;

public class Main {
    private static final long DEFAULT_ITERATIONS = 50000L;

    private static long churnChecksum(long iterations) {
        final String seed = "abcXYZ0123456789";
        StringBuilder builder = new StringBuilder();
        while (builder.length() < 4096) {
            builder.append(seed);
        }
        final String data = builder.toString();

        long checksum = 0;
        for (long k = 0; k < iterations; k++) {
            String s = data;
            for (int round = 0; round < 8; round++) {
                s = s.toUpperCase(Locale.ROOT);
                s = s.substring(3, s.length() - 1);
                s = s.toLowerCase(Locale.ROOT) + s.substring(0, 8);
                s = s.substring(2);
            }
            checksum = (checksum * 31 + s.indexOf("c") + s.length()) % 1000003;
        }
        return checksum;
    }

    public static void main(String[] args) {
        long iterations = DEFAULT_ITERATIONS;
        if (args.length > 0) {
            try {
                iterations = Long.parseLong(args[0]);
            } catch (NumberFormatException ignored) {
                iterations = DEFAULT_ITERATIONS;
            }
        }
        System.out.println(churnChecksum(iterations));
    }
}
