use crate::base::b92::b92;
use crate::base::b4::b4;

pub fn m116(x: i64) -> i64 {
    b92(x) + b4(x + 1)
}

