use crate::mid::m34::m34;
use crate::mid::m111::m111;

pub fn a58(x: i64) -> i64 {
    m34(x) + m111(x + 2)
}

