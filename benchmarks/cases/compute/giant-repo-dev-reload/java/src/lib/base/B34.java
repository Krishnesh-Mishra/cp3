package giant.lib.base;

public class B34 {
    public static int value(int x) {
        int acc = x + 34;
        acc = (acc * 31 + 238) % 1000033;
        acc = (acc * 131 + x * 17 + 34) % 1000199;
        return acc;
    }
}
