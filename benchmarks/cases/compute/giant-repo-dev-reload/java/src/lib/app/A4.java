package giant.lib.app;

import giant.lib.mid.M20;
import giant.lib.mid.M97;

public class A4 {
    public static int value(int x) {
        return M20.value(x) + M97.value(x + 2);
    }
}
