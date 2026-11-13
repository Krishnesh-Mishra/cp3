use crate::mid::m85::m85;
use crate::mid::m34::m34;

pub fn a17(x: i64) -> i64 {
    m85(x) + m34(x + 2)
}

