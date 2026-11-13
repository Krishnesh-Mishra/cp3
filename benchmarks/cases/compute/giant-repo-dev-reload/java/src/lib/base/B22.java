package giant.lib.base;

public class B22 {
    public static int value(int x) {
        int acc = x + 22;
        acc = (acc * 31 + 154) % 1000033;
        acc = (acc * 131 + x * 17 + 22) % 1000199;
        return acc;
    }
}
