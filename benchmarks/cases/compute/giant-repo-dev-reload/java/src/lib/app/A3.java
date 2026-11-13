package giant.lib.app;

import giant.lib.mid.M15;
import giant.lib.mid.M92;

public class A3 {
    public static int value(int x) {
        return M15.value(x) + M92.value(x + 2);
    }
}
