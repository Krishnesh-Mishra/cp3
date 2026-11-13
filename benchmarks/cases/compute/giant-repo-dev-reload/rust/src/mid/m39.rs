use crate::base::b117::b117;
use crate::base::b29::b29;

pub fn m39(x: i64) -> i64 {
    b117(x) + b29(x + 1)
}

