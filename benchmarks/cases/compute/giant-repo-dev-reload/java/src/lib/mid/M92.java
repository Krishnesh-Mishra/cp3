package giant.lib.mid;

import giant.lib.base.B20;
import giant.lib.base.B60;

public class M92 {
    public static int value(int x) {
        return B20.value(x) + B60.value(x + 1);
    }
}
