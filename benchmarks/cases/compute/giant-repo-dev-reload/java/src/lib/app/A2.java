package giant.lib.app;

import giant.lib.mid.M10;
import giant.lib.mid.M87;

public class A2 {
    public static int value(int x) {
        return M10.value(x) + M87.value(x + 2);
    }
}
