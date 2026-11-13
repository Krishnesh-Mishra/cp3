package giant.lib.base;

public class B24 {
    public static int value(int x) {
        int acc = x + 24;
        acc = (acc * 31 + 168) % 1000033;
        acc = (acc * 131 + x * 17 + 24) % 1000199;
        return acc;
    }
}
