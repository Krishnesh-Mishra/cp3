package giant.lib.base;

public class B112 {
    public static int value(int x) {
        int acc = x + 112;
        acc = (acc * 31 + 784) % 1000033;
        acc = (acc * 131 + x * 17 + 112) % 1000199;
        return acc;
    }
}
