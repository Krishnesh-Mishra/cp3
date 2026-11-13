use crate::base::b7::b7;
use crate::base::b47::b47;

pub fn m45(x: i64) -> i64 {
    b7(x) + b47(x + 1)
}

