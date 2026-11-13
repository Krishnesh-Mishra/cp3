use crate::mid::m122::m122;
use crate::mid::m71::m71;

pub fn a50(x: i64) -> i64 {
    m122(x) + m71(x + 2)
}

