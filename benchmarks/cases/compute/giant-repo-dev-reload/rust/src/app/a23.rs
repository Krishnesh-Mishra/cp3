use crate::mid::m115::m115;
use crate::mid::m64::m64;

pub fn a23(x: i64) -> i64 {
    m115(x) + m64(x + 2)
}

