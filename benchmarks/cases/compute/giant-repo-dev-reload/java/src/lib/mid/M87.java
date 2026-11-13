package giant.lib.mid;

import giant.lib.base.B5;
import giant.lib.base.B45;

public class M87 {
    public static int value(int x) {
        return B5.value(x) + B45.value(x + 1);
    }
}
