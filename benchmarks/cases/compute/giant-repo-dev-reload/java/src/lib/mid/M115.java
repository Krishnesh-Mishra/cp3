package giant.lib.mid;

import giant.lib.base.B89;
import giant.lib.base.B1;

public class M115 {
    public static int value(int x) {
        return B89.value(x) + B1.value(x + 1);
    }
}
