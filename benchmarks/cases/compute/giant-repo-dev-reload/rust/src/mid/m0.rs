use crate::base::b0::b0;
use crate::base::b40::b40;

pub fn m0(x: i64) -> i64 {
    b0(x) + b40(x + 1)
}

pub use crate::base::b0::greet;
