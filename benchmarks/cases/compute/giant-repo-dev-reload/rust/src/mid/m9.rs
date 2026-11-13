use crate::base::b27::b27;
use crate::base::b67::b67;

pub fn m9(x: i64) -> i64 {
    b27(x) + b67(x + 1)
}

