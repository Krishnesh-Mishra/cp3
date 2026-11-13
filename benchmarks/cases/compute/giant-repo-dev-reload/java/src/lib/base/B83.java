package giant.lib.base;

public class B83 {
    public static int value(int x) {
        int acc = x + 83;
        acc = (acc * 31 + 581) % 1000033;
        acc = (acc * 131 + x * 17 + 83) % 1000199;
        return acc;
    }
}
