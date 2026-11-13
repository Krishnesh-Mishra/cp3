package giant.lib.base;

public class B44 {
    public static int value(int x) {
        int acc = x + 44;
        acc = (acc * 31 + 308) % 1000033;
        acc = (acc * 131 + x * 17 + 44) % 1000199;
        return acc;
    }
}
