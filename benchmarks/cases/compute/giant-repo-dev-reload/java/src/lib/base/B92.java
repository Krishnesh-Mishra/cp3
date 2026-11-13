package giant.lib.base;

public class B92 {
    public static int value(int x) {
        int acc = x + 92;
        acc = (acc * 31 + 644) % 1000033;
        acc = (acc * 131 + x * 17 + 92) % 1000199;
        return acc;
    }
}
