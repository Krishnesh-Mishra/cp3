use crate::mid::m44::m44;
use crate::mid::m121::m121;

pub fn a60(x: i64) -> i64 {
    m44(x) + m121(x + 2)
}

