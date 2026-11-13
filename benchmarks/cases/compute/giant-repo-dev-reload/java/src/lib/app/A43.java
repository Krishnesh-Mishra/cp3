package giant.lib.app;

import giant.lib.mid.M87;
import giant.lib.mid.M36;

public class A43 {
    public static int value(int x) {
        return M87.value(x) + M36.value(x + 2);
    }
}
