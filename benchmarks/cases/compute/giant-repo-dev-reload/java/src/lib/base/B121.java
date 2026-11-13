package giant.lib.base;

public class B121 {
    public static int value(int x) {
        int acc = x + 121;
        acc = (acc * 31 + 847) % 1000033;
        acc = (acc * 131 + x * 17 + 121) % 1000199;
        return acc;
    }
}
