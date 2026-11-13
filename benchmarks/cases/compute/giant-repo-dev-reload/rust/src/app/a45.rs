use crate::mid::m97::m97;
use crate::mid::m46::m46;

pub fn a45(x: i64) -> i64 {
    m97(x) + m46(x + 2)
}

