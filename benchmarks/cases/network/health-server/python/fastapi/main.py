"""health-server (python entry) -- FastAPI on Uvicorn.

Run:   <venv-python> main.py [port]        (default 8080)
Venv:  benchmarks/.cache/deps/python

Contract, identical in every language entry:
  GET /healthz  -> 200 text/plain "ok"
  GET /         -> 200 text/plain "health-server ok"
  anything else -> 404 text/plain "not found"

Binds 127.0.0.1 only. One Uvicorn worker process.
"""

import sys

import uvicorn
from fastapi import FastAPI, Request
from fastapi.responses import PlainTextResponse
from starlette.exceptions import HTTPException as StarletteHTTPException

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080

app = FastAPI(
    title="health-server",
    docs_url=None,
    redoc_url=None,
    openapi_url=None,
)
app.router.redirect_slashes = False


@app.get("/healthz", response_class=PlainTextResponse)
def healthz() -> str:
    return "ok"


@app.get("/", response_class=PlainTextResponse)
def root() -> str:
    return "health-server ok"


@app.exception_handler(StarletteHTTPException)
async def http_exception_handler(
    request: Request, exc: StarletteHTTPException
) -> PlainTextResponse:
    # FastAPI's default 404 is JSON and a method mismatch is a 405; the bench
    # contract wants text/plain "not found" with status 404 for both.
    if exc.status_code in (404, 405):
        return PlainTextResponse("not found", status_code=404)
    return PlainTextResponse(str(exc.detail), status_code=exc.status_code)


if __name__ == "__main__":
    print(f"listening on http://127.0.0.1:{PORT}", flush=True)
    uvicorn.run(
        app,
        host="127.0.0.1",
        port=PORT,
        log_level="warning",
        access_log=False,
    )
