package giant.lib.mid;

import giant.lib.base.B23;
import giant.lib.base.B63;

public class M93 {
    public static int value(int x) {
        return B23.value(x) + B63.value(x + 1);
    }
}
