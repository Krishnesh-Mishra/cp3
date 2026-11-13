package giant.lib.base;

public class B49 {
    public static int value(int x) {
        int acc = x + 49;
        acc = (acc * 31 + 343) % 1000033;
        acc = (acc * 131 + x * 17 + 49) % 1000199;
        return acc;
    }
}
