use crate::mid::m54::m54;
use crate::mid::m3::m3;

pub fn a62(x: i64) -> i64 {
    m54(x) + m3(x + 2)
}

