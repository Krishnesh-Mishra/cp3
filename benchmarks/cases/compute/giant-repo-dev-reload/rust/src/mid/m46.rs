use crate::base::b10::b10;
use crate::base::b50::b50;

pub fn m46(x: i64) -> i64 {
    b10(x) + b50(x + 1)
}

