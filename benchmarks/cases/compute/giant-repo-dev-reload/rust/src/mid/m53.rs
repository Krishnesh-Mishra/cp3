use crate::base::b31::b31;
use crate::base::b71::b71;

pub fn m53(x: i64) -> i64 {
    b31(x) + b71(x + 1)
}

