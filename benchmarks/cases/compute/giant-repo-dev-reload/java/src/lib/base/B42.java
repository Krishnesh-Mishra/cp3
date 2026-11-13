package giant.lib.base;

public class B42 {
    public static int value(int x) {
        int acc = x + 42;
        acc = (acc * 31 + 294) % 1000033;
        acc = (acc * 131 + x * 17 + 42) % 1000199;
        return acc;
    }
}
