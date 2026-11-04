public class Main {
    static long xsNext(long x) { x ^= x << 13; x ^= x >>> 7; x ^= x << 17; return x; }
    static class TNode { long v; TNode l, r; TNode(long v, TNode l, TNode r){this.v=v;this.l=l;this.r=r;} }
    static TNode build(int depth, long seed) {
        long s = xsNext(seed);
        if (depth == 0) return new TNode(Long.remainderUnsigned(s,1000), null, null);
        return new TNode(Long.remainderUnsigned(s,1000), build(depth-1,s), build(depth-1,xsNext(s+1)));
    }
    static long sumTree(TNode n, long p){ if(n==null) return 0; long s=n.v^p; s+=sumTree(n.l,p); s+=sumTree(n.r,p); return s; }
    public static void main(String[] args){
        TNode root = build(13, 12345);
        long total=0; for(long p=0;p<8;p++) total += sumTree(root,p);
        System.out.println(16383); System.out.println(Long.toUnsignedString(total));
    }
}
