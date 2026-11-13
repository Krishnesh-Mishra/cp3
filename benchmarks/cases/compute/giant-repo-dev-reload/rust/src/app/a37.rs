use crate::mid::m57::m57;
use crate::mid::m6::m6;

pub fn a37(x: i64) -> i64 {
    m57(x) + m6(x + 2)
}

