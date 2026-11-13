use crate::mid::m82::m82;
use crate::mid::m31::m31;

pub fn a42(x: i64) -> i64 {
    m82(x) + m31(x + 2)
}

