package giant.lib.app;

import giant.lib.mid.M82;
import giant.lib.mid.M31;

public class A42 {
    public static int value(int x) {
        return M82.value(x) + M31.value(x + 2);
    }
}
