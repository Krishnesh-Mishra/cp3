package giant.lib.mid;

import giant.lib.base.B65;
import giant.lib.base.B105;

public class M107 {
    public static int value(int x) {
        return B65.value(x) + B105.value(x + 1);
    }
}
