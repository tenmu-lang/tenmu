struct Data { x: i32 }
fn take(d: Data) -> i32 { return d.x }
fn main() -> i32 {
    let mut a = Data { x: 1 }
    let b = a
    a = Data { x: 2 }
    return take(a) + take(b)
}
