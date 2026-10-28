// compute-single (java entry) -- total Collatz steps for every start value in
// 1..1,000,000, printed as one integer.
//
// Same workload and same output as the C++, Rust, Go, JavaScript and Python
// entries.

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
        final long n = 1_000_000L;
        long total = 0;
        for (long start = 1; start <= n; start++) {
            total += collatzSteps(start);
        }
        System.out.println(total);
    }
}
