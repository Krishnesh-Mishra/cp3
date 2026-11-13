use crate::base::b32::b32;
use crate::base::b72::b72;

pub fn m96(x: i64) -> i64 {
    b32(x) + b72(x + 1)
}

