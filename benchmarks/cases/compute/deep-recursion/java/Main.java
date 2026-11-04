// deep-recursion (java entry) -- recurse to depth 5,000 and fold the triangular
// sum into a checksum for 200 rounds. The recursive call is the workload; no
// loop or closed-form replacement is used.
//
// Expected output: 292249866.

public class Main {
    private static final int DEPTH = 5000;
    private static final int ROUNDS = 200;
    private static final long MOD = 1_000_000_007L;

    private static long sumTo(long n) {
        if (n == 0) {
            return 0;
        }
        return n + sumTo(n - 1);
    }

    public static void main(String[] args) {
        long checksum = 0;
        for (int round = 0; round < ROUNDS; round++) {
            checksum = (checksum * 31 + sumTo(DEPTH)) % MOD;
        }

        System.out.println(checksum);
    }
}
