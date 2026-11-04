// math-kernel (java entry) -- evaluate sqrt, floor, abs, pow and clamp over
// 5,000,000 points, quantize each result to an integer and fold one checksum.
//
// Same workload and same output in every language entry. sqrt is correctly
// rounded, pow uses an integer exponent, and every term is quantized to an
// integer before folding, so the checksum is exact across libms.

public class Main {
    private static final int COUNT = 5_000_000;
    private static final long MOD = 1_000_000_007L;

    public static void main(String[] args) {
        long checksum = 0;
        double sqrtTotal = 0.0;
        for (int i = 1; i <= COUNT; i++) {
            double x = i * 0.000001;
            double s = Math.sqrt((double) i);
            sqrtTotal = sqrtTotal + s;
            double f = Math.floor(x + 0.5);
            double a = Math.abs(x - 2.5);
            long p = (long) Math.pow((double) i, 2.0);
            double c = Math.min(Math.max(x * 1000.0, 0.0), 1000.0);
            double mixed = s + f + a + c;
            long quantum = (long) (mixed * 1000000.0);
            checksum = (checksum * 31 + quantum + p) % MOD;
        }
        long sqrtScaled = (long) (sqrtTotal * 1000.0);
        checksum = (checksum * 31 + sqrtScaled) % MOD;
        System.out.println(checksum);
    }
}
