package giant.lib.app;

import giant.lib.mid.M22;
import giant.lib.mid.M99;

public class A30 {
    public static int value(int x) {
        return M22.value(x) + M99.value(x + 2);
    }
}
