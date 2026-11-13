use crate::base::b48::b48;
use crate::base::b88::b88;

pub fn m16(x: i64) -> i64 {
    b48(x) + b88(x + 1)
}

