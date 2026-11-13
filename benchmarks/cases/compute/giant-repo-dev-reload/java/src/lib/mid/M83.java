package giant.lib.mid;

import giant.lib.base.B121;
import giant.lib.base.B33;

public class M83 {
    public static int value(int x) {
        return B121.value(x) + B33.value(x + 1);
    }
}
