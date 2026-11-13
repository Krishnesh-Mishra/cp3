package giant.lib.app;

import giant.lib.mid.M35;
import giant.lib.mid.M112;

public class A7 {
    public static int value(int x) {
        return M35.value(x) + M112.value(x + 2);
    }
}
