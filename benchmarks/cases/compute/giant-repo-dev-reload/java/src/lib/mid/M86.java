package giant.lib.mid;

import giant.lib.base.B2;
import giant.lib.base.B42;

public class M86 {
    public static int value(int x) {
        return B2.value(x) + B42.value(x + 1);
    }
}
