use crate::base::b8::b8;
use crate::base::b48::b48;

pub fn m88(x: i64) -> i64 {
    b8(x) + b48(x + 1)
}

