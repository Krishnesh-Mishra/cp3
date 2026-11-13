use crate::base::b75::b75;
use crate::base::b115::b115;

pub fn m25(x: i64) -> i64 {
    b75(x) + b115(x + 1)
}

