use crate::base::b40::b40;
use crate::base::b80::b80;

pub fn m56(x: i64) -> i64 {
    b40(x) + b80(x + 1)
}

