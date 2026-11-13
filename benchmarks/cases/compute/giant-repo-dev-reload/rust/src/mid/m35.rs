use crate::base::b105::b105;
use crate::base::b17::b17;

pub fn m35(x: i64) -> i64 {
    b105(x) + b17(x + 1)
}

