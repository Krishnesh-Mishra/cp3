package giant.lib.mid;

import giant.lib.base.B96;
import giant.lib.base.B8;

public class M32 {
    public static int value(int x) {
        return B96.value(x) + B8.value(x + 1);
    }
}
