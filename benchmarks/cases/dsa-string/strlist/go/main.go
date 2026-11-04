package main
import "fmt"
func xs_next(x uint64) uint64 { x^=x<<13; x^=x>>7; x^=x<<17; return x }
func main(){ var words []string; r:=uint64(12345); for i:=0;i<100000;i++ { r=xs_next(r); ln:=3+int(r%18); bs:=make([]byte, ln); for j:=0;j<ln;j++ { r=xs_next(r); bs[j]=byte('a'+r%26) }; words=append(words, string(bs)) }; var total uint64; for p:=0;p<4;p++ { for _,w:=range words { total+=uint64(len(w))+uint64(w[0])+uint64(w[len(w)-1]) } }; fmt.Println(len(words)); fmt.Println(total) }