use crate::mid::m100::m100;
use crate::mid::m49::m49;

pub fn a20(x: i64) -> i64 {
    m100(x) + m49(x + 2)
}

