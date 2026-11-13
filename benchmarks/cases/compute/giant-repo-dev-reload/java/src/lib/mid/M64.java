package giant.lib.mid;

import giant.lib.base.B64;
import giant.lib.base.B104;

public class M64 {
    public static int value(int x) {
        return B64.value(x) + B104.value(x + 1);
    }
}
