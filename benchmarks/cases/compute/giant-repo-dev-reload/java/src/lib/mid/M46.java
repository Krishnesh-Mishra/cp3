package giant.lib.mid;

import giant.lib.base.B10;
import giant.lib.base.B50;

public class M46 {
    public static int value(int x) {
        return B10.value(x) + B50.value(x + 1);
    }
}
