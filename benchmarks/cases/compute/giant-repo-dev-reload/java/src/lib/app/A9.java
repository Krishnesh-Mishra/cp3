package giant.lib.app;

import giant.lib.mid.M45;
import giant.lib.mid.M122;

public class A9 {
    public static int value(int x) {
        return M45.value(x) + M122.value(x + 2);
    }
}
