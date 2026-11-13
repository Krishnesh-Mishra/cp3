package giant.lib.mid;

import giant.lib.base.B120;
import giant.lib.base.B32;

public class M40 {
    public static int value(int x) {
        return B120.value(x) + B32.value(x + 1);
    }
}
