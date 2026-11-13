package giant.lib.app;

import giant.lib.mid.M90;
import giant.lib.mid.M39;

public class A18 {
    public static int value(int x) {
        return M90.value(x) + M39.value(x + 2);
    }
}
