package giant.lib.app;

import giant.lib.mid.M100;
import giant.lib.mid.M49;

public class A20 {
    public static int value(int x) {
        return M100.value(x) + M49.value(x + 2);
    }
}
