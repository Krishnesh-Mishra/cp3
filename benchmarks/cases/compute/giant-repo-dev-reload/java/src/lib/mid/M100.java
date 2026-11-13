package giant.lib.mid;

import giant.lib.base.B44;
import giant.lib.base.B84;

public class M100 {
    public static int value(int x) {
        return B44.value(x) + B84.value(x + 1);
    }
}
