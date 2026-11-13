package giant.lib.mid;

import giant.lib.base.B110;
import giant.lib.base.B22;

public class M122 {
    public static int value(int x) {
        return B110.value(x) + B22.value(x + 1);
    }
}
