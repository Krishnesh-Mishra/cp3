package giant.lib.base;

public class B23 {
    public static int value(int x) {
        int acc = x + 23;
        acc = (acc * 31 + 161) % 1000033;
        acc = (acc * 131 + x * 17 + 23) % 1000199;
        return acc;
    }
}
