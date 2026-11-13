package giant.lib.base;

public class B84 {
    public static int value(int x) {
        int acc = x + 84;
        acc = (acc * 31 + 588) % 1000033;
        acc = (acc * 131 + x * 17 + 84) % 1000199;
        return acc;
    }
}
