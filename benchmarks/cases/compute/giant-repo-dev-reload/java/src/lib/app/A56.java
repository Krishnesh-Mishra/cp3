package giant.lib.app;

import giant.lib.mid.M24;
import giant.lib.mid.M101;

public class A56 {
    public static int value(int x) {
        return M24.value(x) + M101.value(x + 2);
    }
}
