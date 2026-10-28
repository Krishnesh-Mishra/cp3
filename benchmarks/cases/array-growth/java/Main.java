// array-growth (java entry) -- push 20,000,000 numbers into a growing array
// while summing them, pop half of them back off, then print the sum.
//
// Same workload and same output as the C++, Rust, Go, JavaScript and Python
// entries. Java has no primitive dynamic array in the standard library, so
// this entry uses the same empty-start/doubling-growth long[] a hand-written
// primitive collection would; the pops are size decrements, the equivalent of
// Go's reslice.

public class Main {
    private static final int PUSHES = 20_000_000;

    public static void main(String[] args) {
        long[] values = new long[0];
        int size = 0;
        long total = 0;
        for (long i = 1; i <= PUSHES; i++) {
            if (size == values.length) {
                int newCapacity = values.length == 0 ? 1 : values.length * 2;
                long[] grown = new long[newCapacity];
                System.arraycopy(values, 0, grown, 0, size);
                values = grown;
            }
            values[size] = i;
            size++;
            total = total + i;
        }

        for (int k = 0; k < PUSHES / 2; k++) {
            size--;
        }

        if (size != PUSHES / 2) {
            throw new IllegalStateException("wrong array size");
        }
        System.out.println(total);
    }
}
