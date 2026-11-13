package giant.lib.app;

import giant.lib.mid.M95;
import giant.lib.mid.M44;

public class A19 {
    public static int value(int x) {
        return M95.value(x) + M44.value(x + 2);
    }
}
