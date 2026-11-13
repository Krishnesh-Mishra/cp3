use crate::base::b118::b118;
use crate::base::b30::b30;

pub fn m82(x: i64) -> i64 {
    b118(x) + b30(x + 1)
}

