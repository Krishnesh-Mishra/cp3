package giant.lib.base;

public class B127 {
    public static int value(int x) {
        int acc = x + 127;
        acc = (acc * 31 + 889) % 1000033;
        acc = (acc * 131 + x * 17 + 127) % 1000199;
        return acc;
    }
}
