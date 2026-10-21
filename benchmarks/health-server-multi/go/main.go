// health-server-multi (go entry) -- net/http, standard library only. net/http
// already serves every connection concurrently on the Go scheduler, and
// GOMAXPROCS defaults to the number of logical CPUs, so this entry is the
// idiomatic multicore Go server.
//
// Build:  go build -o health-server-multi.exe main.go
// Run:    health-server-multi.exe [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only.

package main

import (
	"fmt"
	"net/http"
	"os"
	"runtime"
	"strconv"
)

func main() {
	port := 8080
	if len(os.Args) > 1 {
		if parsed, err := strconv.Atoi(os.Args[1]); err == nil {
			port = parsed
		}
	}
	runtime.GOMAXPROCS(runtime.NumCPU())

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
