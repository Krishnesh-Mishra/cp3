package giant.lib.base;

public class B15 {
    public static int value(int x) {
        int acc = x + 15;
        acc = (acc * 31 + 105) % 1000033;
        acc = (acc * 131 + x * 17 + 15) % 1000199;
        return acc;
    }
}
