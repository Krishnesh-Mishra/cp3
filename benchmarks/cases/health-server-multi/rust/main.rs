// health-server-multi (rust entry) -- Axum 0.8 on Tokio, all cores.
//
// Build:  cargo build --release        (binary: target/release/health-server-multi-rust.exe)
// Run:    target/release/health-server-multi-rust.exe [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only. Tokio's default multi-threaded runtime runs one worker
// thread per logical CPU, so Axum serves requests on every core.

use axum::{
    http::StatusCode,
    response::IntoResponse,
    routing::get,
    Router,
};

async fn healthz() -> &'static str {
    "ok"
}

async fn root() -> &'static str {
    "health-server ok"
}

async fn not_found() -> impl IntoResponse {
    (StatusCode::NOT_FOUND, "not found")
}

#[tokio::main]
async fn main() {
    let port: u16 = std::env::args()
        .nth(1)
        .and_then(|value| value.parse().ok())
        .unwrap_or(8080);

    let app = Router::new()
        .route("/healthz", get(healthz))
        .route("/", get(root))
        .fallback(not_found)
        .method_not_allowed_fallback(not_found);

    let address = format!("127.0.0.1:{port}");
    let listener = tokio::net::TcpListener::bind(&address)
        .await
        .expect("bind failed");
    println!("listening on http://{address}");
    axum::serve(listener, app).await.expect("server failed");
}
