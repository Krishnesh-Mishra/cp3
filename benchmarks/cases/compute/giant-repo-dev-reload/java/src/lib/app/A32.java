package giant.lib.app;

import giant.lib.mid.M32;
import giant.lib.mid.M109;

public class A32 {
    public static int value(int x) {
        return M32.value(x) + M109.value(x + 2);
    }
}
