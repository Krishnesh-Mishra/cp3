use crate::base::b103::b103;
use crate::base::b15::b15;

pub fn m77(x: i64) -> i64 {
    b103(x) + b15(x + 1)
}

