package giant.lib.mid;

import giant.lib.base.B59;
import giant.lib.base.B99;

public class M105 {
    public static int value(int x) {
        return B59.value(x) + B99.value(x + 1);
    }
}
