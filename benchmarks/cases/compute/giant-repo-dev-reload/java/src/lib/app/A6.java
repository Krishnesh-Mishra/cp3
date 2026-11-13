package giant.lib.app;

import giant.lib.mid.M30;
import giant.lib.mid.M107;

public class A6 {
    public static int value(int x) {
        return M30.value(x) + M107.value(x + 2);
    }
}
