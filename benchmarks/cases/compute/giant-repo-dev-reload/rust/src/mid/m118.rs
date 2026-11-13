use crate::base::b98::b98;
use crate::base::b10::b10;

pub fn m118(x: i64) -> i64 {
    b98(x) + b10(x + 1)
}

