use crate::base::b29::b29;
use crate::base::b69::b69;

pub fn m95(x: i64) -> i64 {
    b29(x) + b69(x + 1)
}

