use crate::mid::m30::m30;
use crate::mid::m107::m107;

pub fn a6(x: i64) -> i64 {
    m30(x) + m107(x + 2)
}

