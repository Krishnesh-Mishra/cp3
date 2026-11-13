use crate::mid::m29::m29;
use crate::mid::m106::m106;

pub fn a57(x: i64) -> i64 {
    m29(x) + m106(x + 2)
}

