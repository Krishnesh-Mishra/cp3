public class Main {
    static long xsNext(long x) { x ^= x << 13; x ^= x >>> 7; x ^= x << 17; return x; }
    public static void main(String[] args) {
        int N = 4_000_000, PASSES = 8;
        int[] data = new int[N];
        long r = 12345;
        for (int i = 0; i < N; i++) { r = xsNext(r); data[i] = (int) Long.remainderUnsigned(r, 1000); }
        long total = 0;
        for (int p = 0; p < PASSES; p++) for (int x : data) total += x ^ p;
        System.out.println(N); System.out.println(total);
    }
}
