// closure-capture (java entry) -- two closure shapes back to back over a
// 10,000,000-element source:
//   A: map / filter / reduce with lambdas that capture offset and cap
//   B: 1000 closures, each capturing its own key, stored in an array and
//      invoked after the creating scope has ended (escaping closures)
// Prints both sums on one line. Same workload and same output as the C++,
// Rust, Go, JavaScript and Python entries.

import java.util.Arrays;
import java.util.function.IntUnaryOperator;

public class Main {
    private static final int SOURCE = 10_000_000;
    private static final int OFFSET = 7;
    private static final int CAP = 3;

    public static void main(String[] args) {
        int[] numbers = new int[SOURCE];
        for (int i = 0; i < SOURCE; i++) {
            numbers[i] = i;
        }

        long totalMapReduce = Arrays.stream(numbers)
                .mapToLong(x -> x + OFFSET)
                .filter(x -> x % CAP == 0)
                .sum();

        IntUnaryOperator[] closures = new IntUnaryOperator[1000];
        for (int key = 0; key < 1000; key++) {
            final int bound = key;
            closures[key] = x -> x * bound + OFFSET;
        }

        long totalEscaping = 0;
        for (int index = 0; index < closures.length; index++) {
            for (int call = 0; call < 3000; call++) {
                totalEscaping = totalEscaping + closures[index].applyAsInt(index);
            }
        }

        System.out.println(totalMapReduce + " " + totalEscaping);
    }
}
