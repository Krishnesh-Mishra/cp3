use crate::base::b35::b35;
use crate::base::b75::b75;

pub fn m97(x: i64) -> i64 {
    b35(x) + b75(x + 1)
}

