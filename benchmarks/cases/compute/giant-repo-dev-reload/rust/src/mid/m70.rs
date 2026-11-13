use crate::base::b82::b82;
use crate::base::b122::b122;

pub fn m70(x: i64) -> i64 {
    b82(x) + b122(x + 1)
}

