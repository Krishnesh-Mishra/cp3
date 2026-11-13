package giant.lib.app;

import giant.lib.mid.M80;
import giant.lib.mid.M29;

public class A16 {
    public static int value(int x) {
        return M80.value(x) + M29.value(x + 2);
    }
}
