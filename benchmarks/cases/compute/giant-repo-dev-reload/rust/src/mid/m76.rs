use crate::base::b100::b100;
use crate::base::b12::b12;

pub fn m76(x: i64) -> i64 {
    b100(x) + b12(x + 1)
}

