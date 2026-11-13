use crate::base::b80::b80;
use crate::base::b120::b120;

pub fn m112(x: i64) -> i64 {
    b80(x) + b120(x + 1)
}

