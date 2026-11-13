use crate::mid::m110::m110;
use crate::mid::m59::m59;

pub fn a22(x: i64) -> i64 {
    m110(x) + m59(x + 2)
}

