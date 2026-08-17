import std.io
import std.net.http
import std.encoding.json

struct Greeting {
    message: str,
}

async fn handle(req: http.Request) -> http.Response {
    let name = req.query("name").unwrap_or("world")
    let body = Greeting { message: "Hello, #{name}!" }
    return http.Response.json(200, json.encode(body))
}

fn main() {
    http.serve(":8080", handle)
}
