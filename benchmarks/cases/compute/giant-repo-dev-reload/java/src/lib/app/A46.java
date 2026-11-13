package giant.lib.app;

import giant.lib.mid.M102;
import giant.lib.mid.M51;

public class A46 {
    public static int value(int x) {
        return M102.value(x) + M51.value(x + 2);
    }
}
