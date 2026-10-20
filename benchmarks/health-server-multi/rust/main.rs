// health-server-multi (rust entry) -- std::net only, no crates, with one
// accept/handler worker per available CPU.
//
// Build:  rustc -O main.rs -o health-server-multi.exe
// Run:    health-server-multi.exe [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only. Every worker calls accept() on the same listening
// socket and serves its connection in place (HTTP/1.1 keep-alive), so the
// kernel spreads new connections across the worker threads. Pipelined requests
// are not supported (the bench client never pipelines).

use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::Arc;
use std::thread;

fn main() {
    let port: u16 = std::env::args()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(8080);

    let listener = Arc::new(TcpListener::bind(("127.0.0.1", port)).expect("bind failed"));
    println!("listening on http://127.0.0.1:{}", port);

    let workers = thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(1)
        .max(1);
    let mut handles = Vec::with_capacity(workers);
    for _ in 0..workers {
        let listener = Arc::clone(&listener);
        handles.push(thread::spawn(move || loop {
            match listener.accept() {
                Ok((stream, _)) => handle(stream),
                Err(_) => continue,
            }
        }));
    }
    for handle in handles {
        let _ = handle.join();
    }
}

fn handle(mut stream: TcpStream) {
    let mut buffer = Vec::new();
    loop {
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
