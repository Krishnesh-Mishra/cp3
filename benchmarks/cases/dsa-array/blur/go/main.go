package main
import "fmt"
func xs_next(x uint64) uint64 { x^=x<<13; x^=x>>7; x^=x<<17; return x }
func main(){ w:=256; a:=make([]uint8, w*w); b:=make([]uint8, w*w); r:=uint64(12345); for i:=0;i<w*w;i++ { r=xs_next(r); a[i]=uint8(r%256) }; blur:=func(s,b []uint8){ for y:=1;y<w-1;y++ { for x:=1;x<w-1;x++ { i:=y*w+x; sm:=int(s[i-w-1])+int(s[i-w])*2+int(s[i-w+1])+int(s[i-1])*2+int(s[i])*4+int(s[i+1])*2+int(s[i+w-1])+int(s[i+w])*2+int(s[i+w+1]); b[i]=uint8(sm>>4) } } }; for p:=0;p<8;p++ { blur(a,b); blur(b,a) }; var total int; for _,v:=range a { total+=int(v) }; fmt.Println(w); fmt.Println(total) }