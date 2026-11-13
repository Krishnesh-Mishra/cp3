use crate::base::b45::b45;
use crate::base::b85::b85;

pub fn m15(x: i64) -> i64 {
    b45(x) + b85(x + 1)
}

