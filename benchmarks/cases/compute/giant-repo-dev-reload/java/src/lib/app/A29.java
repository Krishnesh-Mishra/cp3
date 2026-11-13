package giant.lib.app;

import giant.lib.mid.M17;
import giant.lib.mid.M94;

public class A29 {
    public static int value(int x) {
        return M17.value(x) + M94.value(x + 2);
    }
}
