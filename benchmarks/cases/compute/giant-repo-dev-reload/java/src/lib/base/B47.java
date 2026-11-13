package giant.lib.base;

public class B47 {
    public static int value(int x) {
        int acc = x + 47;
        acc = (acc * 31 + 329) % 1000033;
        acc = (acc * 131 + x * 17 + 47) % 1000199;
        return acc;
    }
}
