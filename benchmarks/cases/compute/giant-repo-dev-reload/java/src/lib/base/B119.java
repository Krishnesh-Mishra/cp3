package giant.lib.base;

public class B119 {
    public static int value(int x) {
        int acc = x + 119;
        acc = (acc * 31 + 833) % 1000033;
        acc = (acc * 131 + x * 17 + 119) % 1000199;
        return acc;
    }
}
