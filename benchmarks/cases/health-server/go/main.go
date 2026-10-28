// health-server (go entry) -- Gin on top of net/http.
//
// Build:  go build -o health-server.exe .
// Run:    health-server.exe [port]        (default 8080)
//
// Contract, identical in every language entry:
//   GET /healthz  -> 200 text/plain "ok"
//   GET /         -> 200 text/plain "health-server ok"
//   anything else -> 404 text/plain "not found"
//
// Binds 127.0.0.1 only. Gin runs on the standard net/http server, so every
// request is served on its own goroutine.

package main

import (
	"fmt"
	"net/http"
	"os"
	"strconv"

	"github.com/gin-gonic/gin"
)

func main() {
	port := 8080
	if len(os.Args) > 1 {
		if parsed, err := strconv.Atoi(os.Args[1]); err == nil {
			port = parsed
		}
	}

	gin.SetMode(gin.ReleaseMode)
	router := gin.New()
	router.RedirectTrailingSlash = false
	router.RedirectFixedPath = false
	router.HandleMethodNotAllowed = false

	router.GET("/healthz", func(c *gin.Context) {
		c.Data(http.StatusOK, "text/plain", []byte("ok"))
	})
	router.GET("/", func(c *gin.Context) {
		c.Data(http.StatusOK, "text/plain", []byte("health-server ok"))
	})
	router.NoRoute(func(c *gin.Context) {
		c.Data(http.StatusNotFound, "text/plain", []byte("not found"))
	})

	address := fmt.Sprintf("127.0.0.1:%d", port)
	fmt.Printf("listening on http://%s\n", address)
	if err := router.Run(address); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
