use crate::base::b102::b102;
use crate::base::b14::b14;

pub fn m34(x: i64) -> i64 {
    b102(x) + b14(x + 1)
}

