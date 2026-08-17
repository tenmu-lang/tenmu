struct Point { x: f64, y: f64 }

fn distance(a: Point, b: Point) -> f64 {
    let dx = a.x - b.x
    let dy = a.y - b.y
    return dx * dx + dy * dy
}

fn main() -> i32 {
    let p1 = Point { x: 0.0, y: 0.0 }
    let p2 = Point { x: 3.0, y: 4.0 }
    let d = distance(p1, p2)
    if d > 0.0 {
        return 1
    } else {
        return 0
    }
}
