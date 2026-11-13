package giant.lib.app;

import giant.lib.mid.M4;
import giant.lib.mid.M81;

public class A52 {
    public static int value(int x) {
        return M4.value(x) + M81.value(x + 2);
    }
}
