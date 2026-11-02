# postgres-registration

An HTTP registration service backed by PostgreSQL. It installs the
[`postgres-cp3`](https://github.com/Krishnesh-Mishra/postgres-cp3) package with
the normal package flow, creates a `users` table on startup, and writes a row
for every `POST /register`.

## Prerequisites

- A running PostgreSQL server (17.x is what the package is tested against).
- The database and user named by the environment variables below. The default
  values point at a local `postgres` database owned by `postgres`.

## Install and run

```sh
cp3 install https://github.com/Krishnesh-Mishra/postgres-cp3
cp3 run src/index.cp3
```

`cp3 install` records the package in `.cp3/lock.json` and installs its platform
native bundle beside the project. No vendored copy or manual native setup is
needed.

Configuration is read from the environment:

| Variable | Default | Meaning |
| --- | --- | --- |
| `CP3_PG_HOST` | `localhost` | PostgreSQL host |
| `CP3_PG_PORT` | `5432` | PostgreSQL port |
| `CP3_PG_DB` | `postgres` | Database name |
| `CP3_PG_USER` | `postgres` | Database user |
| `CP3_PG_PASSWORD` | `postgres` | Database password |
| `PORT` | `3000` | HTTP listen port |

For example:

```sh
CP3_PG_DB=app CP3_PG_USER=app CP3_PG_PASSWORD=secret PORT=8080 cp3 run src/index.cp3
```

The service creates this table if it does not exist:

```sql
CREATE TABLE IF NOT EXISTS users (
  id BIGSERIAL PRIMARY KEY,
  name TEXT NOT NULL,
  email TEXT NOT NULL UNIQUE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

## Try it

```sh
curl -i -X POST http://localhost:3000/register \
  -H "Content-Type: application/json" \
  -d '{"name":"Ada Lovelace","email":"ada@example.com"}'
```

```json
{"id":1,"name":"Ada Lovelace","email":"ada@example.com"}
```

Registering the same email again returns `409` with PostgreSQL's duplicate-key
message. Read a registration back by id:

```sh
curl http://localhost:3000/users/1
```

```json
{"id":1,"name":"Ada Lovelace","email":"ada@example.com"}
```

`GET /healthz` returns `{"status":"ok"}`.

The server runs with one worker thread because a PostgreSQL connection belongs
to one thread at a time; the package API is synchronous by design.
