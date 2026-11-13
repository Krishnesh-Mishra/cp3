package giant.lib.base;

public class B16 {
    public static int value(int x) {
        int acc = x + 16;
        acc = (acc * 31 + 112) % 1000033;
        acc = (acc * 131 + x * 17 + 16) % 1000199;
        return acc;
    }
}
