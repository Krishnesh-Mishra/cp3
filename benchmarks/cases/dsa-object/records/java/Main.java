import java.util.*;
public class Main{
 static long xsNext(long x){x^=x<<13;x^=x>>>7;x^=x<<17;return x;}
 static class E{int kind;long id,a,b;int w0;long wlen;}
 public static void main(String[]args){ List<E> log=new ArrayList<>(100000); long r=12345; for(int i=0;i<100000;i++){ r=xsNext(r); long k=Long.remainderUnsigned(r,8); long id=Long.remainderUnsigned(r>>>3,100000); E e=new E(); e.id=id; if(k<4){e.kind=1; e.a=Long.remainderUnsigned(r>>>20,201)-100; e.b=Long.remainderUnsigned(r>>>40,201)-100;} else if(k<6){e.kind=2; e.wlen=4+Long.remainderUnsigned(r,12); long w=xsNext(r); e.w0=(int)(Long.remainderUnsigned(w,26))+97;} else if(k<7){e.kind=3; e.a=Long.remainderUnsigned(r>>>20,256);} else e.kind=0; log.add(e); } long total=0; for(int p=0;p<4;p++) for(E e:log){ if(e.kind==0) total+=1; else if(e.kind==1) total+=e.id+e.a+e.b; else if(e.kind==2) total+=e.id+e.wlen+e.w0; else total+=e.id+e.a; } System.out.println(log.size()); System.out.println(total); }
}