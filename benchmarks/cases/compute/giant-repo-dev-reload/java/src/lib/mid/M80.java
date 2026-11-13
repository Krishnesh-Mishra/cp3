package giant.lib.mid;

import giant.lib.base.B112;
import giant.lib.base.B24;

public class M80 {
    public static int value(int x) {
        return B112.value(x) + B24.value(x + 1);
    }
}
