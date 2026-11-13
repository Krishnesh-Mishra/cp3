package giant.lib.app;

import giant.lib.mid.M42;
import giant.lib.mid.M119;

public class A34 {
    public static int value(int x) {
        return M42.value(x) + M119.value(x + 2);
    }
}
