import std.io
import std.net.http

fn handle(req: http.Request) -> http.Response {
    let name = req.query("name").unwrap_or("world")
    return http.Response.text(200, "Hello, #{name}!")
}

struct Point { x: f64, y: f64 }

fn main() -> i32 {
    let x = 0x1F
    let y = 3.14e10
    let c = 'a'
    let big = 1_000_000
    if x > 10 && y < 100.0 {
        io.println("ok")
    }
    return 0
}
