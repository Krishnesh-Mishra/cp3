package giant.lib.base;

public class B71 {
    public static int value(int x) {
        int acc = x + 71;
        acc = (acc * 31 + 497) % 1000033;
        acc = (acc * 131 + x * 17 + 71) % 1000199;
        return acc;
    }
}
