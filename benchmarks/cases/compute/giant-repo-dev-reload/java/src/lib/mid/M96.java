package giant.lib.mid;

import giant.lib.base.B32;
import giant.lib.base.B72;

public class M96 {
    public static int value(int x) {
        return B32.value(x) + B72.value(x + 1);
    }
}
