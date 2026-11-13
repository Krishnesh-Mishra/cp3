package giant.lib.mid;

import giant.lib.base.B100;
import giant.lib.base.B12;

public class M76 {
    public static int value(int x) {
        return B100.value(x) + B12.value(x + 1);
    }
}
