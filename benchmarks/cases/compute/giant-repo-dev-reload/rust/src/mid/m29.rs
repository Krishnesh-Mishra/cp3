use crate::base::b87::b87;
use crate::base::b127::b127;

pub fn m29(x: i64) -> i64 {
    b87(x) + b127(x + 1)
}

