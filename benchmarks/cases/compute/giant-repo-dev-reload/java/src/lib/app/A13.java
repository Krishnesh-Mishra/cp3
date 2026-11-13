package giant.lib.app;

import giant.lib.mid.M65;
import giant.lib.mid.M14;

public class A13 {
    public static int value(int x) {
        return M65.value(x) + M14.value(x + 2);
    }
}
