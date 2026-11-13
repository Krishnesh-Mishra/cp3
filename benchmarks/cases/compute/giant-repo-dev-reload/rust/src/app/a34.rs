use crate::mid::m42::m42;
use crate::mid::m119::m119;

pub fn a34(x: i64) -> i64 {
    m42(x) + m119(x + 2)
}

