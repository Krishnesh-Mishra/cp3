package giant.lib.app;

import giant.lib.mid.M105;
import giant.lib.mid.M54;

public class A21 {
    public static int value(int x) {
        return M105.value(x) + M54.value(x + 2);
    }
}
