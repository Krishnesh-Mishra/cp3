package giant.lib.mid;

import giant.lib.base.B71;
import giant.lib.base.B111;

public class M109 {
    public static int value(int x) {
        return B71.value(x) + B111.value(x + 1);
    }
}
