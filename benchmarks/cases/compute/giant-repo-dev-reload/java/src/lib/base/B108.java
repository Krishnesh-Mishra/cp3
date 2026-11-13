package giant.lib.base;

public class B108 {
    public static int value(int x) {
        int acc = x + 108;
        acc = (acc * 31 + 756) % 1000033;
        acc = (acc * 131 + x * 17 + 108) % 1000199;
        return acc;
    }
}
