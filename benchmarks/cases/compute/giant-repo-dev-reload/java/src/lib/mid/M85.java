package giant.lib.mid;

import giant.lib.base.B127;
import giant.lib.base.B39;

public class M85 {
    public static int value(int x) {
        return B127.value(x) + B39.value(x + 1);
    }
}
