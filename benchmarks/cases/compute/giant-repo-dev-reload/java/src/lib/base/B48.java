package giant.lib.base;

public class B48 {
    public static int value(int x) {
        int acc = x + 48;
        acc = (acc * 31 + 336) % 1000033;
        acc = (acc * 131 + x * 17 + 48) % 1000199;
        return acc;
    }
}
