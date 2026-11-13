package giant.lib.base;

public class B87 {
    public static int value(int x) {
        int acc = x + 87;
        acc = (acc * 31 + 609) % 1000033;
        acc = (acc * 131 + x * 17 + 87) % 1000199;
        return acc;
    }
}
