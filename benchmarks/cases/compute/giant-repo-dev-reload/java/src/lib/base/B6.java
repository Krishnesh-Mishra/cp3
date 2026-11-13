package giant.lib.base;

public class B6 {
    public static int value(int x) {
        int acc = x + 6;
        acc = (acc * 31 + 42) % 1000033;
        acc = (acc * 131 + x * 17 + 6) % 1000199;
        return acc;
    }
}
