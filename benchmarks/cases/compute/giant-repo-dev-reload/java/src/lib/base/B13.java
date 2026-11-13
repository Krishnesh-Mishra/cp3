package giant.lib.base;

public class B13 {
    public static int value(int x) {
        int acc = x + 13;
        acc = (acc * 31 + 91) % 1000033;
        acc = (acc * 131 + x * 17 + 13) % 1000199;
        return acc;
    }
}
