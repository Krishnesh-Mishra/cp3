package main
import "fmt"
func xs_next(x uint64) uint64 { x^=x<<13; x^=x>>7; x^=x<<17; return x }
type Item struct{key,val uint64}
func main(){ items:=make([]Item,0,1000000); marks:=make([]uint64,0); r:=uint64(12345); for i:=uint64(0);i<1000000;i++ { r=xs_next(r); items=append(items, Item{r%1000000,i%97}); if i%64==0 { marks=append(marks,uint64(len(items)-1)) } }; var total,mt uint64; for _,it:=range items { total+=it.key }; for _,m:=range marks { mt+=items[m].val }; fmt.Println(len(items)); fmt.Println(len(marks)); fmt.Println(total); fmt.Println(mt) }