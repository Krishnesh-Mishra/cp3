use crate::base::b78::b78;
use crate::base::b118::b118;

pub fn m26(x: i64) -> i64 {
    b78(x) + b118(x + 1)
}

