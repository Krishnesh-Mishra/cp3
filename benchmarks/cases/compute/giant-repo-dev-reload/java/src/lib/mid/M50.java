package giant.lib.mid;

import giant.lib.base.B22;
import giant.lib.base.B62;

public class M50 {
    public static int value(int x) {
        return B22.value(x) + B62.value(x + 1);
    }
}
