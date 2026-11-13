package giant.lib.base;

public class B78 {
    public static int value(int x) {
        int acc = x + 78;
        acc = (acc * 31 + 546) % 1000033;
        acc = (acc * 131 + x * 17 + 78) % 1000199;
        return acc;
    }
}
