package giant.lib.mid;

import giant.lib.base.B62;
import giant.lib.base.B102;

public class M106 {
    public static int value(int x) {
        return B62.value(x) + B102.value(x + 1);
    }
}
