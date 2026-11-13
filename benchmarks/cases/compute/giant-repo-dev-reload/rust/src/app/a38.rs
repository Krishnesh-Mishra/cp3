use crate::mid::m62::m62;
use crate::mid::m11::m11;

pub fn a38(x: i64) -> i64 {
    m62(x) + m11(x + 2)
}

