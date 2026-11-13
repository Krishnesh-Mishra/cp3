use crate::base::b20::b20;
use crate::base::b60::b60;

pub fn m92(x: i64) -> i64 {
    b20(x) + b60(x + 1)
}

