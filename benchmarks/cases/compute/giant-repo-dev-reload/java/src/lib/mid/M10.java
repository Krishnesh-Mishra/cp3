package giant.lib.mid;

import giant.lib.base.B30;
import giant.lib.base.B70;

public class M10 {
    public static int value(int x) {
        return B30.value(x) + B70.value(x + 1);
    }
}
