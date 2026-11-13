use crate::mid::m17::m17;
use crate::mid::m94::m94;

pub fn a29(x: i64) -> i64 {
    m17(x) + m94(x + 2)
}

