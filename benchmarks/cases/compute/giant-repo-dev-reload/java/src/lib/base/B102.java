package giant.lib.base;

public class B102 {
    public static int value(int x) {
        int acc = x + 102;
        acc = (acc * 31 + 714) % 1000033;
        acc = (acc * 131 + x * 17 + 102) % 1000199;
        return acc;
    }
}
