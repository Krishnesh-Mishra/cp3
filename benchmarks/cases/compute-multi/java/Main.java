// compute-multi (java entry) -- the compute-single workload split across all
// cores with a parallel stream.
//
// LongStream.parallel() partitions 1..1,000,000 across the ForkJoin common
// pool and sums the per-worker partials, so the result is the same single
// integer as every other entry (single or multi).

import java.util.stream.LongStream;

public class Main {
    private static long collatzSteps(long start) {
        long n = start;
        long steps = 0;
        while (n != 1) {
            if (n % 2 == 0) {
                n = n / 2;
            } else {
                n = 3 * n + 1;
            }
            steps = steps + 1;
        }
        return steps;
    }

    public static void main(String[] args) {
        long total = LongStream.rangeClosed(1L, 1_000_000L)
                .parallel()
                .map(Main::collatzSteps)
                .sum();
        System.out.println(total);
    }
}
