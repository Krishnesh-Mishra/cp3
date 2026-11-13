package giant.lib.mid;

import giant.lib.base.B24;
import giant.lib.base.B64;

public class M8 {
    public static int value(int x) {
        return B24.value(x) + B64.value(x + 1);
    }
}
