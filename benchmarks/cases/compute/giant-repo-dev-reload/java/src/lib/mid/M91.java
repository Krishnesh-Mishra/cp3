package giant.lib.mid;

import giant.lib.base.B17;
import giant.lib.base.B57;

public class M91 {
    public static int value(int x) {
        return B17.value(x) + B57.value(x + 1);
    }
}
