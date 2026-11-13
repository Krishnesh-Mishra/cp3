use crate::base::b79::b79;
use crate::base::b119::b119;

pub fn m69(x: i64) -> i64 {
    b79(x) + b119(x + 1)
}

