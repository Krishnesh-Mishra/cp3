use crate::base::b73::b73;
use crate::base::b113::b113;

pub fn m67(x: i64) -> i64 {
    b73(x) + b113(x + 1)
}

