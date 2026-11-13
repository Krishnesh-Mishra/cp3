use crate::mid::m50::m50;
use crate::mid::m127::m127;

pub fn a10(x: i64) -> i64 {
    m50(x) + m127(x + 2)
}

