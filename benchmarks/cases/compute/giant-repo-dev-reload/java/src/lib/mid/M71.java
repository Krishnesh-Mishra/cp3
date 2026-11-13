package giant.lib.mid;

import giant.lib.base.B85;
import giant.lib.base.B125;

public class M71 {
    public static int value(int x) {
        return B85.value(x) + B125.value(x + 1);
    }
}
