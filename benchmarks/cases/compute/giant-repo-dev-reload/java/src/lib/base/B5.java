package giant.lib.base;

public class B5 {
    public static int value(int x) {
        int acc = x + 5;
        acc = (acc * 31 + 35) % 1000033;
        acc = (acc * 131 + x * 17 + 5) % 1000199;
        return acc;
    }
}
