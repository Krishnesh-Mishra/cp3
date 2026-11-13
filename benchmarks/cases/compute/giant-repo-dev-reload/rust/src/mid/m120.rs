use crate::base::b104::b104;
use crate::base::b16::b16;

pub fn m120(x: i64) -> i64 {
    b104(x) + b16(x + 1)
}

