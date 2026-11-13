package giant.lib.app;

import giant.lib.mid.M122;
import giant.lib.mid.M71;

public class A50 {
    public static int value(int x) {
        return M122.value(x) + M71.value(x + 2);
    }
}
