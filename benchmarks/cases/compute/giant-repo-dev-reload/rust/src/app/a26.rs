use crate::mid::m2::m2;
use crate::mid::m79::m79;

pub fn a26(x: i64) -> i64 {
    m2(x) + m79(x + 2)
}

