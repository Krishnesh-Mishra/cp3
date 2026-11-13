use crate::base::b115::b115;
use crate::base::b27::b27;

pub fn m81(x: i64) -> i64 {
    b115(x) + b27(x + 1)
}

