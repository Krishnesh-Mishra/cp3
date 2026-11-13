package giant.lib.app;

import giant.lib.mid.M12;
import giant.lib.mid.M89;

public class A28 {
    public static int value(int x) {
        return M12.value(x) + M89.value(x + 2);
    }
}
