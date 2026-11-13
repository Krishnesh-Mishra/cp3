package giant.lib.base;

public class B96 {
    public static int value(int x) {
        int acc = x + 96;
        acc = (acc * 31 + 672) % 1000033;
        acc = (acc * 131 + x * 17 + 96) % 1000199;
        return acc;
    }
}
