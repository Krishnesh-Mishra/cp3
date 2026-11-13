package giant.lib.app;

import giant.lib.mid.M60;
import giant.lib.mid.M9;

public class A12 {
    public static int value(int x) {
        return M60.value(x) + M9.value(x + 2);
    }
}
