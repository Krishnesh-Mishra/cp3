package giant.lib.app;

import giant.lib.mid.M75;
import giant.lib.mid.M24;

public class A15 {
    public static int value(int x) {
        return M75.value(x) + M24.value(x + 2);
    }
}
