package giant.lib.app;

import giant.lib.mid.M72;
import giant.lib.mid.M21;

public class A40 {
    public static int value(int x) {
        return M72.value(x) + M21.value(x + 2);
    }
}
