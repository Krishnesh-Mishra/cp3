package giant.lib.mid;

import giant.lib.base.B52;
import giant.lib.base.B92;

public class M60 {
    public static int value(int x) {
        return B52.value(x) + B92.value(x + 1);
    }
}
