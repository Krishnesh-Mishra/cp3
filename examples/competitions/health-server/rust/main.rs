// health-server (rust entry) -- std::net only, no crates.
//
// Build:  rustc -O main.rs -o health-server.exe
// Run:    health-server.exe [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only. One thread per connection, HTTP/1.1 keep-alive: a
// connection stays open and serves requests until the client closes it.
// Pipelined requests are not supported (the bench client never pipelines).

use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::thread;

fn main() {
    let port: u16 = std::env::args()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(8080);

    let listener = TcpListener::bind(("127.0.0.1", port)).expect("bind failed");
    println!("listening on http://127.0.0.1:{}", port);

    for stream in listener.incoming() {
        if let Ok(stream) = stream {
            thread::spawn(move || handle(stream));
        }
    }
}

fn handle(mut stream: TcpStream) {
    let mut buffer = Vec::new();
    loop {
        // Accumulate one request head (up to the blank line).
        while !buffer.windows(4).any(|w| w == b"\r\n\r\n") {
            let mut chunk = [0u8; 4096];
            match stream.read(&mut chunk) {
                Ok(0) => return,
                Ok(n) => buffer.extend_from_slice(&chunk[..n]),
                Err(_) => return,
            }
            if buffer.len() > 65536 {
                return;
            }
        }
        let end = buffer.windows(4).position(|w| w == b"\r\n\r\n").unwrap() + 4;
        let head = String::from_utf8_lossy(&buffer[..end]).to_string();
        buffer.drain(..end);

        let mut parts = head.lines().next().unwrap_or("").split_whitespace();
        let method = parts.next().unwrap_or("");
        let path = parts.next().unwrap_or("");

        let (status, reason, body) = if method == "GET" && path == "/healthz" {
            (200, "OK", "ok")
        } else if method == "GET" && path == "/" {
            (200, "OK", "health-server ok")
        } else {
            (404, "Not Found", "not found")
        };

        let response = format!(
            "HTTP/1.1 {} {}\r\nContent-Type: text/plain\r\nContent-Length: {}\r\nConnection: keep-alive\r\n\r\n{}",
            status,
            reason,
            body.len(),
            body
        );
        if stream.write_all(response.as_bytes()).is_err() {
            return;
        }
    }
}
