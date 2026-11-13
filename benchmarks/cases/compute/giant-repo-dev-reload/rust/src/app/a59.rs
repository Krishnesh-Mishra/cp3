use crate::mid::m39::m39;
use crate::mid::m116::m116;

pub fn a59(x: i64) -> i64 {
    m39(x) + m116(x + 2)
}

