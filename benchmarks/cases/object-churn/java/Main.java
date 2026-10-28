// object-churn (java entry) -- create 5,000,000 small objects in a hot loop:
// each iteration's temporary dies in scope, and every second object escapes
// into an array. One checksum at the end.
//
// Same workload and same output as the C++, Rust, Go, JavaScript and Python
// entries. The 2.5M survivors are real garbage-collector pressure; the
// temporary may be scalar-replaced by the JIT, which is the honest split for
// this runtime.

import java.util.ArrayList;
import java.util.List;

public class Main {
    private static final int OBJECTS = 5_000_000;
    private static final long MOD = 1_000_000_007L;

    private static final class Cell {
        final long value;

        Cell(long value) {
            this.value = value;
        }
    }

    public static void main(String[] args) {
        List<Cell> kept = new ArrayList<>();
        long acc = 0;
        for (int i = 0; i < OBJECTS; i++) {
            Cell temp = new Cell(i);
            acc = (acc + temp.value) % MOD;
            if (i % 2 == 0) {
                kept.add(new Cell(i));
            }
        }

        long checksum = acc;
        for (int k = 0; k < kept.size(); k++) {
            checksum = (checksum * 31 + kept.get(k).value) % MOD;
        }
        System.out.println(checksum);
    }
}
