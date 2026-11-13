package giant.lib.base;

public class B7 {
    public static int value(int x) {
        int acc = x + 7;
        acc = (acc * 31 + 49) % 1000033;
        acc = (acc * 131 + x * 17 + 7) % 1000199;
        return acc;
    }
}
