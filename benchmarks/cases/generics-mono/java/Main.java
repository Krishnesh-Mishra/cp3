// generics-mono (java entry) -- the same 2,000,000-iteration sum run twice:
// once through a generic wrapper type plus a generic function, once through a
// hand-written concrete type. Prints both sums on one line.
//
// Same workload and same output as the C++, Rust, Go, JavaScript and Python
// entries. Java generics are erased, so the generic Box<Long> path stores a
// boxed value while the concrete NumberBox path stores a primitive; that is
// the honest cost split for this runtime.

public class Main {
    private static final long ITERATIONS = 2_000_000L;
    private static final long MOD = 1_000_000_007L;

    private static final class Box<T> {
        T value;

        T get() {
            return value;
        }
    }

    private static <T> T sumBox(Box<T> box) {
        return box.get();
    }

    private static final class NumberBox {
        final long value;

        NumberBox(long value) {
            this.value = value;
        }

        long get() {
            return value;
        }
    }

    public static void main(String[] args) {
        long totalGeneric = 0;
        for (long i = 0; i < ITERATIONS; i++) {
            Box<Long> box = new Box<>();
            box.value = i;
            totalGeneric = (totalGeneric + sumBox(box)) % MOD;
        }

        long totalConcrete = 0;
        for (long i = 0; i < ITERATIONS; i++) {
            NumberBox box = new NumberBox(i);
            totalConcrete = (totalConcrete + box.get()) % MOD;
        }

        System.out.println(totalGeneric + " " + totalConcrete);
    }
}
