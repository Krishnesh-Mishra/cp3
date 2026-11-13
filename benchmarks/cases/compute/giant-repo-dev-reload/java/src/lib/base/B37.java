package giant.lib.base;

public class B37 {
    public static int value(int x) {
        int acc = x + 37;
        acc = (acc * 31 + 259) % 1000033;
        acc = (acc * 131 + x * 17 + 37) % 1000199;
        return acc;
    }
}
