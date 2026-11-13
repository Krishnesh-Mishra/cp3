package giant.lib.app;

import giant.lib.mid.M2;
import giant.lib.mid.M79;

public class A26 {
    public static int value(int x) {
        return M2.value(x) + M79.value(x + 2);
    }
}
