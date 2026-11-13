package giant.lib.mid;

import giant.lib.base.B101;
import giant.lib.base.B13;

public class M119 {
    public static int value(int x) {
        return B101.value(x) + B13.value(x + 1);
    }
}
