package giant.lib.base;

public class B66 {
    public static int value(int x) {
        int acc = x + 66;
        acc = (acc * 31 + 462) % 1000033;
        acc = (acc * 131 + x * 17 + 66) % 1000199;
        return acc;
    }
}
