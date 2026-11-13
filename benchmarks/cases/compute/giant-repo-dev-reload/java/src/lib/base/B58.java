package giant.lib.base;

public class B58 {
    public static int value(int x) {
        int acc = x + 58;
        acc = (acc * 31 + 406) % 1000033;
        acc = (acc * 131 + x * 17 + 58) % 1000199;
        return acc;
    }
}
