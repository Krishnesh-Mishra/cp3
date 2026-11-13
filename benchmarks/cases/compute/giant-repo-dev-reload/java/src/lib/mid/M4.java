package giant.lib.mid;

import giant.lib.base.B12;
import giant.lib.base.B52;

public class M4 {
    public static int value(int x) {
        return B12.value(x) + B52.value(x + 1);
    }
}
