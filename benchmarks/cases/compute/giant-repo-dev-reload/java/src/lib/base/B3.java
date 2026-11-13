package giant.lib.base;

public class B3 {
    public static int value(int x) {
        int acc = x + 3;
        acc = (acc * 31 + 21) % 1000033;
        acc = (acc * 131 + x * 17 + 3) % 1000199;
        return acc;
    }
}
