use crate::base::b127::b127;
use crate::base::b39::b39;

pub fn m85(x: i64) -> i64 {
    b127(x) + b39(x + 1)
}

