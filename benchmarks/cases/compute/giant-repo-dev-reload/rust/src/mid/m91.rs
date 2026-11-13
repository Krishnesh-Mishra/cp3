use crate::base::b17::b17;
use crate::base::b57::b57;

pub fn m91(x: i64) -> i64 {
    b17(x) + b57(x + 1)
}

