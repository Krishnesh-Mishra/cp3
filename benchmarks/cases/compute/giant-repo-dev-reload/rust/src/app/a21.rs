use crate::mid::m105::m105;
use crate::mid::m54::m54;

pub fn a21(x: i64) -> i64 {
    m105(x) + m54(x + 2)
}

