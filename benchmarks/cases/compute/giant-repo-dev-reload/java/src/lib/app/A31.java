package giant.lib.app;

import giant.lib.mid.M27;
import giant.lib.mid.M104;

public class A31 {
    public static int value(int x) {
        return M27.value(x) + M104.value(x + 2);
    }
}
