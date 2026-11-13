package giant.lib.app;

import giant.lib.mid.M112;
import giant.lib.mid.M61;

public class A48 {
    public static int value(int x) {
        return M112.value(x) + M61.value(x + 2);
    }
}
