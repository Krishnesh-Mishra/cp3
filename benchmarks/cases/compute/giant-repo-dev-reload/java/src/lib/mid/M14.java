package giant.lib.mid;

import giant.lib.base.B42;
import giant.lib.base.B82;

public class M14 {
    public static int value(int x) {
        return B42.value(x) + B82.value(x + 1);
    }
}
