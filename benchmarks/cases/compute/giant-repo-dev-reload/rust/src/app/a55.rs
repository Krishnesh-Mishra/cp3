use crate::mid::m19::m19;
use crate::mid::m96::m96;

pub fn a55(x: i64) -> i64 {
    m19(x) + m96(x + 2)
}

