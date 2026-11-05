package main
import "fmt"
func xs_next(x uint64) uint64 { x^=x<<13; x^=x>>7; x^=x<<17; return x }
const M=int64(1000003)
type Expr struct{ kind int; v int64; l,r *Expr }
func build(depth uint64, seed uint64) *Expr { s:=xs_next(seed); if depth==0 { return &Expr{kind:0, v:int64(s%1000)} }; k:=s%8; if k<1 { return &Expr{kind:3, l:build(depth-1,s)} }; a:=build(depth-1,s); b:=build(depth-1,xs_next(s)); if k<5 { return &Expr{kind:1, l:a, r:b} }; return &Expr{kind:2, l:a, r:b} }
func eval(e *Expr,p int64) int64 { switch e.kind { case 0: return (e.v+p)%M; case 1: return (eval(e.l,p)+eval(e.r,p))%M; case 2: return (eval(e.l,p)*eval(e.r,p))%M; default: return (M-eval(e.l,p))%M } }
func count(e *Expr) int64 { if e==nil { return 0 }; return 1+count(e.l)+count(e.r) }
func main(){ r:=build(4,12345); var total int64; for p:=int64(0);p<8;p++ { total=(total+eval(r,p))%M }; fmt.Println(count(r)); fmt.Println(total) }