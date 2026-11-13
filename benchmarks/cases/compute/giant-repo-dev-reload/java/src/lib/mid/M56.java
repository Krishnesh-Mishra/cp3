package giant.lib.mid;

import giant.lib.base.B40;
import giant.lib.base.B80;

public class M56 {
    public static int value(int x) {
        return B40.value(x) + B80.value(x + 1);
    }
}
