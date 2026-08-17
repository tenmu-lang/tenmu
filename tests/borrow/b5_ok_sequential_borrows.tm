struct Data { x: i32 }
fn read(d: &Data) -> i32 { return d.x }
fn main() -> i32 {
    let mut a = Data { x: 1 }
    {
        let r1 = &mut a
    }
    let r2 = &mut a
    return read(r2)
}
