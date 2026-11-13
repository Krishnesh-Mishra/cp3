package giant.lib.mid;

import giant.lib.base.B58;
import giant.lib.base.B98;

public class M62 {
    public static int value(int x) {
        return B58.value(x) + B98.value(x + 1);
    }
}
