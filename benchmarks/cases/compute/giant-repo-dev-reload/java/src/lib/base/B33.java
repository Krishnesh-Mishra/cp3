package giant.lib.base;

public class B33 {
    public static int value(int x) {
        int acc = x + 33;
        acc = (acc * 31 + 231) % 1000033;
        acc = (acc * 131 + x * 17 + 33) % 1000199;
        return acc;
    }
}
