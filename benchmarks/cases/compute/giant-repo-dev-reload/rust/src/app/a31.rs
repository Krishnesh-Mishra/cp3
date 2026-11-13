use crate::mid::m27::m27;
use crate::mid::m104::m104;

pub fn a31(x: i64) -> i64 {
    m27(x) + m104(x + 2)
}

