package giant.lib.base;

public class B97 {
    public static int value(int x) {
        int acc = x + 97;
        acc = (acc * 31 + 679) % 1000033;
        acc = (acc * 131 + x * 17 + 97) % 1000199;
        return acc;
    }
}
