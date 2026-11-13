package giant.lib.app;

import giant.lib.mid.M70;
import giant.lib.mid.M19;

public class A14 {
    public static int value(int x) {
        return M70.value(x) + M19.value(x + 2);
    }
}
