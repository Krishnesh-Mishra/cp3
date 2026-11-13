package giant.lib.base;

public class B36 {
    public static int value(int x) {
        int acc = x + 36;
        acc = (acc * 31 + 252) % 1000033;
        acc = (acc * 131 + x * 17 + 36) % 1000199;
        return acc;
    }
}
