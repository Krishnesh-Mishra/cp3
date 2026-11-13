package giant.lib.mid;

import giant.lib.base.B50;
import giant.lib.base.B90;

public class M102 {
    public static int value(int x) {
        return B50.value(x) + B90.value(x + 1);
    }
}
