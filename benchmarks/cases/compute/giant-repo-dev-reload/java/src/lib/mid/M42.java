package giant.lib.mid;

import giant.lib.base.B126;
import giant.lib.base.B38;

public class M42 {
    public static int value(int x) {
        return B126.value(x) + B38.value(x + 1);
    }
}
