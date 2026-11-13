package giant.lib.base;

public class B82 {
    public static int value(int x) {
        int acc = x + 82;
        acc = (acc * 31 + 574) % 1000033;
        acc = (acc * 131 + x * 17 + 82) % 1000199;
        return acc;
    }
}
