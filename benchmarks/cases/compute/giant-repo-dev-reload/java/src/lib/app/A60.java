package giant.lib.app;

import giant.lib.mid.M44;
import giant.lib.mid.M121;

public class A60 {
    public static int value(int x) {
        return M44.value(x) + M121.value(x + 2);
    }
}
