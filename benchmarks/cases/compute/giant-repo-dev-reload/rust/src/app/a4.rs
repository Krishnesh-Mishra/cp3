use crate::mid::m20::m20;
use crate::mid::m97::m97;

pub fn a4(x: i64) -> i64 {
    m20(x) + m97(x + 2)
}

