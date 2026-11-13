package giant.lib.mid;

import giant.lib.base.B3;
import giant.lib.base.B43;

public class M1 {
    public static int value(int x) {
        return B3.value(x) + B43.value(x + 1);
    }
}
