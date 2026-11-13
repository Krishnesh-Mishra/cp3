package giant.lib.base;

public class B126 {
    public static int value(int x) {
        int acc = x + 126;
        acc = (acc * 31 + 882) % 1000033;
        acc = (acc * 131 + x * 17 + 126) % 1000199;
        return acc;
    }
}
