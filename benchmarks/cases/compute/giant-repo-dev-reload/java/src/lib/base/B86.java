package giant.lib.base;

public class B86 {
    public static int value(int x) {
        int acc = x + 86;
        acc = (acc * 31 + 602) % 1000033;
        acc = (acc * 131 + x * 17 + 86) % 1000199;
        return acc;
    }
}
