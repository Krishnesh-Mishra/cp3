package giant.lib.base;

public class B54 {
    public static int value(int x) {
        int acc = x + 54;
        acc = (acc * 31 + 378) % 1000033;
        acc = (acc * 131 + x * 17 + 54) % 1000199;
        return acc;
    }
}
