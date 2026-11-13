package giant.lib.base;

public class B60 {
    public static int value(int x) {
        int acc = x + 60;
        acc = (acc * 31 + 420) % 1000033;
        acc = (acc * 131 + x * 17 + 60) % 1000199;
        return acc;
    }
}
