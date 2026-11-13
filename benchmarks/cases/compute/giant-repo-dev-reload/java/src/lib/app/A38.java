package giant.lib.app;

import giant.lib.mid.M62;
import giant.lib.mid.M11;

public class A38 {
    public static int value(int x) {
        return M62.value(x) + M11.value(x + 2);
    }
}
