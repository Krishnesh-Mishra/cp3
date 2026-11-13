use crate::mid::m12::m12;
use crate::mid::m89::m89;

pub fn a28(x: i64) -> i64 {
    m12(x) + m89(x + 2)
}

