package giant.lib.mid;

import giant.lib.base.B36;
import giant.lib.base.B76;

public class M12 {
    public static int value(int x) {
        return B36.value(x) + B76.value(x + 1);
    }
}
