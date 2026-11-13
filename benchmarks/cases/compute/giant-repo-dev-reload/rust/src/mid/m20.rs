use crate::base::b60::b60;
use crate::base::b100::b100;

pub fn m20(x: i64) -> i64 {
    b60(x) + b100(x + 1)
}

