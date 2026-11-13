package giant.lib.base;

public class B104 {
    public static int value(int x) {
        int acc = x + 104;
        acc = (acc * 31 + 728) % 1000033;
        acc = (acc * 131 + x * 17 + 104) % 1000199;
        return acc;
    }
}
