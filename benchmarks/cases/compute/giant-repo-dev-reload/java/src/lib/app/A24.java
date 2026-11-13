package giant.lib.app;

import giant.lib.mid.M120;
import giant.lib.mid.M69;

public class A24 {
    public static int value(int x) {
        return M120.value(x) + M69.value(x + 2);
    }
}
