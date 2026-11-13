package giant.lib.base;

public class B90 {
    public static int value(int x) {
        int acc = x + 90;
        acc = (acc * 31 + 630) % 1000033;
        acc = (acc * 131 + x * 17 + 90) % 1000199;
        return acc;
    }
}
