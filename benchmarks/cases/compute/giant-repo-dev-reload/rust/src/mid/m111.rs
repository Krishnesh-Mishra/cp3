use crate::base::b77::b77;
use crate::base::b117::b117;

pub fn m111(x: i64) -> i64 {
    b77(x) + b117(x + 1)
}

