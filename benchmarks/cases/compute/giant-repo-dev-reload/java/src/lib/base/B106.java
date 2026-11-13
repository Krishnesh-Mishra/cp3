package giant.lib.base;

public class B106 {
    public static int value(int x) {
        int acc = x + 106;
        acc = (acc * 31 + 742) % 1000033;
        acc = (acc * 131 + x * 17 + 106) % 1000199;
        return acc;
    }
}
