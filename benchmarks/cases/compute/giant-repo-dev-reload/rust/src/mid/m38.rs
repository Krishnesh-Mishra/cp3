use crate::base::b114::b114;
use crate::base::b26::b26;

pub fn m38(x: i64) -> i64 {
    b114(x) + b26(x + 1)
}

