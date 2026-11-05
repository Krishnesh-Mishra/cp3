import java.util.*;
public class Main{
 static long xsNext(long x){x^=x<<13;x^=x>>>7;x^=x<<17;return x;}
 static int count=0;
 static class Node{int id,tx,ty,tz,rot; List<Node> kids=new ArrayList<>(); Node(int id,int tx,int ty,int tz,int rot){this.id=id;this.tx=tx;this.ty=ty;this.tz=tz;this.rot=rot;}}
 static Node build(int depth,long seed){ long r=xsNext(seed); Node n=new Node(count++,(int)Long.remainderUnsigned(r,9),(int)Long.remainderUnsigned(r>>>16,9),(int)Long.remainderUnsigned(r>>>24,9),(int)Long.remainderUnsigned(r>>>12,4)); if(depth==0) return n; long k=1+Long.remainderUnsigned(r>>>40,4); for(long i=0;i<k;i++) n.kids.add(build(depth-1,xsNext(r+i))); return n; }
 static void animate(Node n,long p){ if(n.id%7==p){n.rot=(n.rot+1)%4; n.tx=(n.tx+1)%9;} for(Node c:n.kids) animate(c,p); }
 static long gather(Node n){ long t=n.tx+n.ty+n.tz; for(Node c:n.kids) t+=gather(c); return t; }
 public static void main(String[]a){ Node root=build(4,12345); long total=0; for(long f=0;f<16;f++){ animate(root,f%7); total=(total+gather(root))%1000000007; } System.out.println(count); System.out.println(total); }
}