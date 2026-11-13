package giant.lib.base;

public class B118 {
    public static int value(int x) {
        int acc = x + 118;
        acc = (acc * 31 + 826) % 1000033;
        acc = (acc * 131 + x * 17 + 118) % 1000199;
        return acc;
    }
}
