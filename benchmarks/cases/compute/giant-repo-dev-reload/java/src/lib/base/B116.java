package giant.lib.base;

public class B116 {
    public static int value(int x) {
        int acc = x + 116;
        acc = (acc * 31 + 812) % 1000033;
        acc = (acc * 131 + x * 17 + 116) % 1000199;
        return acc;
    }
}
