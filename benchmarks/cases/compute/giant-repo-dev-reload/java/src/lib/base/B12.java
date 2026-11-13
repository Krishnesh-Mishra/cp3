package giant.lib.base;

public class B12 {
    public static int value(int x) {
        int acc = x + 12;
        acc = (acc * 31 + 84) % 1000033;
        acc = (acc * 131 + x * 17 + 12) % 1000199;
        return acc;
    }
}
