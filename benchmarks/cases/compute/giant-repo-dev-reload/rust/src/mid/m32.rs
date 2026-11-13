use crate::base::b96::b96;
use crate::base::b8::b8;

pub fn m32(x: i64) -> i64 {
    b96(x) + b8(x + 1)
}

