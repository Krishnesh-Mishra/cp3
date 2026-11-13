package giant.lib.app;

import giant.lib.mid.M125;
import giant.lib.mid.M74;

public class A25 {
    public static int value(int x) {
        return M125.value(x) + M74.value(x + 2);
    }
}
