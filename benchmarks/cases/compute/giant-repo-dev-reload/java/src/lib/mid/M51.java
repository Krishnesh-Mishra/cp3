package giant.lib.mid;

import giant.lib.base.B25;
import giant.lib.base.B65;

public class M51 {
    public static int value(int x) {
        return B25.value(x) + B65.value(x + 1);
    }
}
