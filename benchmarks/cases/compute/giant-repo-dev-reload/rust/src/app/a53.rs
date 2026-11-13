use crate::mid::m9::m9;
use crate::mid::m86::m86;

pub fn a53(x: i64) -> i64 {
    m9(x) + m86(x + 2)
}

