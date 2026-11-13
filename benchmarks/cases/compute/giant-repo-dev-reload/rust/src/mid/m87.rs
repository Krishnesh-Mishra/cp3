use crate::base::b5::b5;
use crate::base::b45::b45;

pub fn m87(x: i64) -> i64 {
    b5(x) + b45(x + 1)
}

