package giant.lib.app;

import giant.lib.mid.M52;
import giant.lib.mid.M1;

public class A36 {
    public static int value(int x) {
        return M52.value(x) + M1.value(x + 2);
    }
}
