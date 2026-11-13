use crate::mid::m15::m15;
use crate::mid::m92::m92;

pub fn a3(x: i64) -> i64 {
    m15(x) + m92(x + 2)
}

