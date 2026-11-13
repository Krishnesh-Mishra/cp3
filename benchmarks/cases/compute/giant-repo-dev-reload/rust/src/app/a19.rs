use crate::mid::m95::m95;
use crate::mid::m44::m44;

pub fn a19(x: i64) -> i64 {
    m95(x) + m44(x + 2)
}

