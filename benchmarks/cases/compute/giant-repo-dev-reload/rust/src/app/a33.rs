use crate::mid::m37::m37;
use crate::mid::m114::m114;

pub fn a33(x: i64) -> i64 {
    m37(x) + m114(x + 2)
}

