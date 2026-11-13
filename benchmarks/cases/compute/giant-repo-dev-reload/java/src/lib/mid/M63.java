package giant.lib.mid;

import giant.lib.base.B61;
import giant.lib.base.B101;

public class M63 {
    public static int value(int x) {
        return B61.value(x) + B101.value(x + 1);
    }
}
