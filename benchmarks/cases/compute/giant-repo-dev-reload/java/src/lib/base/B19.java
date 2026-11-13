package giant.lib.base;

public class B19 {
    public static int value(int x) {
        int acc = x + 19;
        acc = (acc * 31 + 133) % 1000033;
        acc = (acc * 131 + x * 17 + 19) % 1000199;
        return acc;
    }
}
