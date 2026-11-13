package giant.lib.base;

public class B4 {
    public static int value(int x) {
        int acc = x + 4;
        acc = (acc * 31 + 28) % 1000033;
        acc = (acc * 131 + x * 17 + 4) % 1000199;
        return acc;
    }
}
