package giant.lib.base;

public class B27 {
    public static int value(int x) {
        int acc = x + 27;
        acc = (acc * 31 + 189) % 1000033;
        acc = (acc * 131 + x * 17 + 27) % 1000199;
        return acc;
    }
}
