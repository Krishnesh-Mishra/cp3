use crate::base::b4::b4;
use crate::base::b44::b44;

pub fn m44(x: i64) -> i64 {
    b4(x) + b44(x + 1)
}

