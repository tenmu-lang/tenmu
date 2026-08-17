comptime fn loop_forever(n: i32) -> i32 {
    return loop_forever(n + 1)
}
const BOOM: i32 = loop_forever(0)
fn main() -> i32 { return 0 }
