use crate::base::b56::b56;
use crate::base::b96::b96;

pub fn m104(x: i64) -> i64 {
    b56(x) + b96(x + 1)
}

