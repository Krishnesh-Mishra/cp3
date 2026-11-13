package giant.lib.base;

public class B64 {
    public static int value(int x) {
        int acc = x + 64;
        acc = (acc * 31 + 448) % 1000033;
        acc = (acc * 131 + x * 17 + 64) % 1000199;
        return acc;
    }
}
