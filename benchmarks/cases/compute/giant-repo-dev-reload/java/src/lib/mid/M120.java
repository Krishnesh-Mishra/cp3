package giant.lib.mid;

import giant.lib.base.B104;
import giant.lib.base.B16;

public class M120 {
    public static int value(int x) {
        return B104.value(x) + B16.value(x + 1);
    }
}
