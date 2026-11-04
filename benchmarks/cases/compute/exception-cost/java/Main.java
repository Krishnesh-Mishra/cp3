// exception-cost (java entry) -- run 1,000,000 predicate calls twice: once
// throwing a fresh RuntimeException on every third input, once returning a -1
// sentinel. Both paths fold the same values into separate checksums.
//
// Expected output: 446742259 446742259.

public class Main {
    private static final int ITERATIONS = 1_000_000;
    private static final long MOD = 1_000_000_007L;

    private static long mayThrow(long i) {
        if (i % 3 == 0) {
            throw new RuntimeException("boom");
        }
        return i;
    }

    private static long mayReturn(long i) {
        if (i % 3 == 0) {
            return -1;
        }
        return i;
    }

    public static void main(String[] args) {
        long thrown = 0;
        for (long i = 0; i < ITERATIONS; i++) {
            try {
                long v = mayThrow(i);
                thrown = (thrown * 31 + v) % MOD;
            } catch (RuntimeException e) {
                thrown = (thrown * 31 + 1) % MOD;
            }
        }

        long returned = 0;
        for (long i = 0; i < ITERATIONS; i++) {
            long v = mayReturn(i);
            if (v < 0) {
                returned = (returned * 31 + 1) % MOD;
            } else {
                returned = (returned * 31 + v) % MOD;
            }
        }

        System.out.println(thrown + " " + returned);
    }
}
