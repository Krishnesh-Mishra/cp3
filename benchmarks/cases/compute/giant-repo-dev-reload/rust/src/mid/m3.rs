use crate::base::b9::b9;
use crate::base::b49::b49;

pub fn m3(x: i64) -> i64 {
    b9(x) + b49(x + 1)
}

