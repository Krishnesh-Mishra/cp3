package giant.lib.base;

public class B94 {
    public static int value(int x) {
        int acc = x + 94;
        acc = (acc * 31 + 658) % 1000033;
        acc = (acc * 131 + x * 17 + 94) % 1000199;
        return acc;
    }
}
