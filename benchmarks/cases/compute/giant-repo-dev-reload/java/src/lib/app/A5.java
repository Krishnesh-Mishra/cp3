package giant.lib.app;

import giant.lib.mid.M25;
import giant.lib.mid.M102;

public class A5 {
    public static int value(int x) {
        return M25.value(x) + M102.value(x + 2);
    }
}
