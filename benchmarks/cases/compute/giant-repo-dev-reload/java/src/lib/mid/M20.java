package giant.lib.mid;

import giant.lib.base.B60;
import giant.lib.base.B100;

public class M20 {
    public static int value(int x) {
        return B60.value(x) + B100.value(x + 1);
    }
}
