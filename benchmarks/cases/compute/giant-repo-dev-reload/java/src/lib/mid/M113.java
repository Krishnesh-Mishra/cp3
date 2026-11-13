package giant.lib.mid;

import giant.lib.base.B83;
import giant.lib.base.B123;

public class M113 {
    public static int value(int x) {
        return B83.value(x) + B123.value(x + 1);
    }
}
