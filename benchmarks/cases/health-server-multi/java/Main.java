// health-server-multi (java entry) -- Undertow using all hardware threads.
//
// Build (from the repository root):
//   powershell -ExecutionPolicy Bypass -File benchmarks/runner/setup-java-undertow.ps1
//   javac -cp "benchmarks/.cache/deps/java/undertow/*" -d <classes-dir> benchmarks/cases/health-server-multi/java/Main.java
// Run:
//   java -cp "<classes-dir>;benchmarks/.cache/deps/java/undertow/*" Main [port]
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only. One I/O event loop per available processor and two
// request workers per processor, so all cores participate.

import io.undertow.Undertow;
import io.undertow.server.HttpHandler;
import io.undertow.server.HttpServerExchange;
import io.undertow.util.Headers;
import io.undertow.util.StatusCodes;

public final class Main {
    private static final HttpHandler ROUTES = exchange -> {
        if (!"GET".equals(exchange.getRequestMethod().toString())) {
            send(exchange, StatusCodes.NOT_FOUND, "not found");
            return;
        }

        switch (exchange.getRequestPath()) {
            case "/healthz" -> send(exchange, StatusCodes.OK, "ok");
            case "/" -> send(exchange, StatusCodes.OK, "health-server ok");
            default -> send(exchange, StatusCodes.NOT_FOUND, "not found");
        }
    };

    private static void send(HttpServerExchange exchange, int status, String body) {
        exchange.setStatusCode(status);
        exchange.getResponseHeaders().put(Headers.CONTENT_TYPE, "text/plain");
        exchange.getResponseSender().send(body);
    }

    public static void main(String[] args) {
        int port = 8080;
        if (args.length > 0) {
            try {
                port = Integer.parseInt(args[0]);
            } catch (NumberFormatException e) {
                System.err.println("invalid port: " + args[0]);
                System.exit(2);
            }
        }
        if (port < 1 || port > 65535) {
            System.err.println("port out of range: " + port);
            System.exit(2);
        }

        int processors = Math.max(1, Runtime.getRuntime().availableProcessors());
        Undertow server = Undertow.builder()
                .addHttpListener(port, "127.0.0.1")
                .setIoThreads(processors)
                .setWorkerThreads(processors * 2)
                .setHandler(exchange -> exchange.dispatch(ROUTES))
                .build();

        server.start();
        System.out.println("listening on http://127.0.0.1:" + port);
        System.out.flush();
    }
}
