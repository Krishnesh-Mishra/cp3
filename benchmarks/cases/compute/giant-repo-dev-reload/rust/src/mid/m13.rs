use crate::base::b39::b39;
use crate::base::b79::b79;

pub fn m13(x: i64) -> i64 {
    b39(x) + b79(x + 1)
}

