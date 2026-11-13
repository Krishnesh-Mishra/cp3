package giant.lib.app;

import giant.lib.mid.M110;
import giant.lib.mid.M59;

public class A22 {
    public static int value(int x) {
        return M110.value(x) + M59.value(x + 2);
    }
}
