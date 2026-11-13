package giant.lib.mid;

import giant.lib.base.B123;
import giant.lib.base.B35;

public class M41 {
    public static int value(int x) {
        return B123.value(x) + B35.value(x + 1);
    }
}
