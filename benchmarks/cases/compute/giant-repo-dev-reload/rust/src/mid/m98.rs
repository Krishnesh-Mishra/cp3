use crate::base::b38::b38;
use crate::base::b78::b78;

pub fn m98(x: i64) -> i64 {
    b38(x) + b78(x + 1)
}

