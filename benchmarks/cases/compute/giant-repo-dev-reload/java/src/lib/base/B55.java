package giant.lib.base;

public class B55 {
    public static int value(int x) {
        int acc = x + 55;
        acc = (acc * 31 + 385) % 1000033;
        acc = (acc * 131 + x * 17 + 55) % 1000199;
        return acc;
    }
}
