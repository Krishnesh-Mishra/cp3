package giant.lib.app;

import giant.lib.mid.M40;
import giant.lib.mid.M117;

public class A8 {
    public static int value(int x) {
        return M40.value(x) + M117.value(x + 2);
    }
}
