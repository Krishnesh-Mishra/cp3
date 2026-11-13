package giant.lib.base;

public class B91 {
    public static int value(int x) {
        int acc = x + 91;
        acc = (acc * 31 + 637) % 1000033;
        acc = (acc * 131 + x * 17 + 91) % 1000199;
        return acc;
    }
}
