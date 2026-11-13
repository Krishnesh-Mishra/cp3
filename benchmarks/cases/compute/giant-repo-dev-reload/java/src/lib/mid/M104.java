package giant.lib.mid;

import giant.lib.base.B56;
import giant.lib.base.B96;

public class M104 {
    public static int value(int x) {
        return B56.value(x) + B96.value(x + 1);
    }
}
