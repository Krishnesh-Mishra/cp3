package giant.lib.app;

import giant.lib.mid.M115;
import giant.lib.mid.M64;

public class A23 {
    public static int value(int x) {
        return M115.value(x) + M64.value(x + 2);
    }
}
