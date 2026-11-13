package giant.lib.mid;

import giant.lib.base.B111;
import giant.lib.base.B23;

public class M37 {
    public static int value(int x) {
        return B111.value(x) + B23.value(x + 1);
    }
}
