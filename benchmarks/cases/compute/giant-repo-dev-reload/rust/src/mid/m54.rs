use crate::base::b34::b34;
use crate::base::b74::b74;

pub fn m54(x: i64) -> i64 {
    b34(x) + b74(x + 1)
}

