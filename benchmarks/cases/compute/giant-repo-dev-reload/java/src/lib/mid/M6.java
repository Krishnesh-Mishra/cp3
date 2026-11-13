package giant.lib.mid;

import giant.lib.base.B18;
import giant.lib.base.B58;

public class M6 {
    public static int value(int x) {
        return B18.value(x) + B58.value(x + 1);
    }
}
