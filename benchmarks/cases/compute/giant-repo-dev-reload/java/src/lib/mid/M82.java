package giant.lib.mid;

import giant.lib.base.B118;
import giant.lib.base.B30;

public class M82 {
    public static int value(int x) {
        return B118.value(x) + B30.value(x + 1);
    }
}
