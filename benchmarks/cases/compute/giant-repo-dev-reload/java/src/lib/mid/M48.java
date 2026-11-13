package giant.lib.mid;

import giant.lib.base.B16;
import giant.lib.base.B56;

public class M48 {
    public static int value(int x) {
        return B16.value(x) + B56.value(x + 1);
    }
}
