use crate::base::b120::b120;
use crate::base::b32::b32;

pub fn m40(x: i64) -> i64 {
    b120(x) + b32(x + 1)
}

