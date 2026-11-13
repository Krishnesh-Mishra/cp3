package giant.lib.app;

import giant.lib.mid.M57;
import giant.lib.mid.M6;

public class A37 {
    public static int value(int x) {
        return M57.value(x) + M6.value(x + 2);
    }
}
