package giant.lib.app;

import giant.lib.mid.M7;
import giant.lib.mid.M84;

public class A27 {
    public static int value(int x) {
        return M7.value(x) + M84.value(x + 2);
    }
}
