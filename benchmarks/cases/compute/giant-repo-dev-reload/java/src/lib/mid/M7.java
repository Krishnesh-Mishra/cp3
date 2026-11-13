package giant.lib.mid;

import giant.lib.base.B21;
import giant.lib.base.B61;

public class M7 {
    public static int value(int x) {
        return B21.value(x) + B61.value(x + 1);
    }
}
