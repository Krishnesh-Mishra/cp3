use crate::mid::m90::m90;
use crate::mid::m39::m39;

pub fn a18(x: i64) -> i64 {
    m90(x) + m39(x + 2)
}

