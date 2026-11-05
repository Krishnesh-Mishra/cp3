package main
import "fmt"
func xs_next(x uint64) uint64 { x^=x<<13; x^=x>>7; x^=x<<17; return x }
var text []byte
func gen(depth uint64, seed uint64) uint64 { r:=xs_next(seed); if depth==0||r%4==0 { if (r>>8)%2==0 { n:=3+r%12; for i:=uint64(0);i<n;i++ { r=xs_next(r); text=append(text, byte('a'+r%26)) } } else { text=append(text, []byte(fmt.Sprint((r>>16)%100000))...) }; text=append(text, ' '); return r }; text=append(text, '('); kids:=2+((r>>24)%4); for k:=uint64(0);k<kids;k++ { r=gen(depth-1,r) }; text=append(text, ')'); text=append(text, ' '); return r }
func main(){ seed:=uint64(12345); for i:=0;i<100;i++ { seed=gen(4,seed) }; h:=uint64(1469598103934665603); for _,c:=range text { h=(h^uint64(c))*1099511628211 }; fmt.Println(len(text)); fmt.Println(h) }