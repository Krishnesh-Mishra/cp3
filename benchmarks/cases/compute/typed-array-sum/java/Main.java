// typed-array-sum (java entry) -- fill a fixed double[] and byte[], sum the
// floats for 32 rounds, then fold the float and byte totals into one integer
// checksum.
//
// Same workload and same output in every language entry. The float pattern is
// a multiple of 0.25 and every partial sum stays far below 2^53, so the
// addition is exact in IEEE-754 doubles.

public class Main {
    private static final int SIZE = 1_000_000;
    private static final int ROUNDS = 32;
    private static final long MOD = 1_000_000_007L;

    public static void main(String[] args) {
        double[] values = new double[SIZE];
        byte[] bytes = new byte[SIZE];
        for (int i = 0; i < SIZE; i++) {
            values[i] = (i % 97) * 0.5 + (i % 13) * 0.25;
            bytes[i] = (byte) ((i * 31 + 7) % 256);
        }

        double total = 0.0;
        for (int round = 0; round < ROUNDS; round++) {
            double acc = 0.0;
            for (int i = 0; i < SIZE; i++) {
                acc = acc + values[i];
            }
            total = total + acc;
        }

        long byteTotal = 0;
        for (int i = 0; i < SIZE; i++) {
            byteTotal = byteTotal + (bytes[i] & 0xff);
        }

        long scaled = (long) (total * 4.0);
        long checksum = (scaled + byteTotal) % MOD;
        checksum = (checksum * 31 + SIZE + ROUNDS) % MOD;
        System.out.println(checksum);
    }
}
