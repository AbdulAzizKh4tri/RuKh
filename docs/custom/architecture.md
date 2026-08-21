@page architecture Architecture

# INCOMPLETE FILE

RuKh combines a coroutine-driven networking runtime, an HTTP layer, a thread pool for blocking work, and an asynchronous boundary around SQLite.

## Runtime

Document the relationship between `Executor`, `Task`, `epoll`, `io_uring`, worker threads, `TcpStream`, and `TlsStream`.

## HTTP Layer

```text
listener
  -> connection
  -> request parsing
  -> middleware
  -> router
  -> handler
  -> response
```

Document where parsing, body streaming, compression, range handling, and keep-alive behavior occur.

## Thread Pool

Explain how blocking database work is dispatched to the thread pool and how completion resumes the coroutine.

## Database Layer

```text
ORM / application
        |
    IDatabase
        |
    Sqlite3Db
        |
 ConnectionQueue
        |
    sqlite3*
```

Document connection ownership, prepared-statement caching, transactions, and SQLite configuration.

## ORM

Document model metadata, query builders, predicates, relations, SQL generation, and hydration.

## Error Flow

Document the path from SQLite errors to `DatabaseError`, and HTTP errors through `ErrorFactory`.

## Design Constraints

Record important invariants and compile-time checks here.
