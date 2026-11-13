package giant.lib.base;

public class B14 {
    public static int value(int x) {
        int acc = x + 14;
        acc = (acc * 31 + 98) % 1000033;
        acc = (acc * 131 + x * 17 + 14) % 1000199;
        return acc;
    }
}
