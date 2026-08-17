comptime fn bad_div() -> i32 {
    return 10 / 0
}
const OOPS: i32 = bad_div()
fn main() -> i32 { return 0 }
