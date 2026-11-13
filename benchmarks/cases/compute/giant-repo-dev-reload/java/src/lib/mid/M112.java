package giant.lib.mid;

import giant.lib.base.B80;
import giant.lib.base.B120;

public class M112 {
    public static int value(int x) {
        return B80.value(x) + B120.value(x + 1);
    }
}
