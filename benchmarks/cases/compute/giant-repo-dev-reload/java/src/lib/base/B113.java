package giant.lib.base;

public class B113 {
    public static int value(int x) {
        int acc = x + 113;
        acc = (acc * 31 + 791) % 1000033;
        acc = (acc * 131 + x * 17 + 113) % 1000199;
        return acc;
    }
}
