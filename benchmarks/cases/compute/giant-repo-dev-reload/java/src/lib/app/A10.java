package giant.lib.app;

import giant.lib.mid.M50;
import giant.lib.mid.M127;

public class A10 {
    public static int value(int x) {
        return M50.value(x) + M127.value(x + 2);
    }
}
