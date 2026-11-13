package giant.lib.base;

public class B28 {
    public static int value(int x) {
        int acc = x + 28;
        acc = (acc * 31 + 196) % 1000033;
        acc = (acc * 131 + x * 17 + 28) % 1000199;
        return acc;
    }
}
