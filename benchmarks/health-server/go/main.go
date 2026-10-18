// health-server (go entry) -- net/http, standard library only.
//
// Build:  go build -o health-server.exe main.go
// Run:    health-server.exe [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only. net/http serves each request on its own goroutine.

package main

import (
	"fmt"
	"net/http"
	"os"
	"strconv"
)

func main() {
	port := 8080
	if len(os.Args) > 1 {
		if parsed, err := strconv.Atoi(os.Args[1]); err == nil {
			port = parsed
		}
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprint(w, "ok")
	})
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			w.WriteHeader(http.StatusNotFound)
			fmt.Fprint(w, "not found")
			return
		}
		fmt.Fprint(w, "health-server ok")
	})

	address := fmt.Sprintf("127.0.0.1:%d", port)
	fmt.Printf("listening on http://%s\n", address)
	if err := http.ListenAndServe(address, mux); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
