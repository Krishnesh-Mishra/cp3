package giant.lib.mid;

import giant.lib.base.B41;
import giant.lib.base.B81;

public class M99 {
    public static int value(int x) {
        return B41.value(x) + B81.value(x + 1);
    }
}
