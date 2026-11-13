package giant.lib.base;

public class B95 {
    public static int value(int x) {
        int acc = x + 95;
        acc = (acc * 31 + 665) % 1000033;
        acc = (acc * 131 + x * 17 + 95) % 1000199;
        return acc;
    }
}
