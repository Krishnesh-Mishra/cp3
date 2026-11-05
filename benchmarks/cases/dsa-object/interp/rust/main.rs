fn xs_next(mut x: u64) -> u64 { x ^= x << 13; x ^= x >> 7; x ^= x << 17; x }
const M: i64=1000003;
enum E { Num(i64), Add(Box<E>,Box<E>), Mul(Box<E>,Box<E>), Neg(Box<E>) }
fn build(depth: u64, seed: u64) -> E { let s=xs_next(seed); if depth==0 { return E::Num((s%1000) as i64); } let k=s%8; if k<1 { return E::Neg(Box::new(build(depth-1,s))); } let a=build(depth-1,s); let b=build(depth-1,xs_next(s)); if k<5 { E::Add(Box::new(a),Box::new(b)) } else { E::Mul(Box::new(a),Box::new(b)) } }
fn eval(e:&E,p:i64)->i64{ match e { E::Num(v)=>(v+p)%M, E::Add(l,r)=>(eval(l,p)+eval(r,p))%M, E::Mul(l,r)=>(eval(l,p)*eval(r,p))%M, E::Neg(o)=>(M-eval(o,p))%M } }
fn count(e:&E)->i64{ match e{ E::Num(_)=>1, E::Add(l,r)|E::Mul(l,r)=>1+count(l)+count(r), E::Neg(o)=>1+count(o)} }
fn main(){ let r=build(4,12345); let c=count(&r); let mut total:i64=0; for p in 0..8 { total=(total+eval(&r,p))%M; } println!("{}",c); println!("{}",total); }