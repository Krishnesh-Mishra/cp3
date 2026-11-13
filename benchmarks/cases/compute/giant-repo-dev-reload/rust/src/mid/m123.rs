use crate::base::b113::b113;
use crate::base::b25::b25;

pub fn m123(x: i64) -> i64 {
    b113(x) + b25(x + 1)
}

