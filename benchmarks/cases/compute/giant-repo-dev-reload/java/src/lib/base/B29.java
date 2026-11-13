package giant.lib.base;

public class B29 {
    public static int value(int x) {
        int acc = x + 29;
        acc = (acc * 31 + 203) % 1000033;
        acc = (acc * 131 + x * 17 + 29) % 1000199;
        return acc;
    }
}
