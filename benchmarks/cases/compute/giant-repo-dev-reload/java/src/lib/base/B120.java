package giant.lib.base;

public class B120 {
    public static int value(int x) {
        int acc = x + 120;
        acc = (acc * 31 + 840) % 1000033;
        acc = (acc * 131 + x * 17 + 120) % 1000199;
        return acc;
    }
}
