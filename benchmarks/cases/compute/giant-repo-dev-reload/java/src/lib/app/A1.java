package giant.lib.app;

import giant.lib.mid.M5;
import giant.lib.mid.M82;

public class A1 {
    public static int value(int x) {
        return M5.value(x) + M82.value(x + 2);
    }
}
