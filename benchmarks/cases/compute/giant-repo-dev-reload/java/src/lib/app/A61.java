package giant.lib.app;

import giant.lib.mid.M49;
import giant.lib.mid.M126;

public class A61 {
    public static int value(int x) {
        return M49.value(x) + M126.value(x + 2);
    }
}
