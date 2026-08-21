@page features Feature List

A feature-level overview of **RuKh**.

**Note:** The ORM query builder is subject to change substantially.

---

# Networking / Core Runtime
- Edge-triggered `epoll` event loop (`EPOLLIN | EPOLLOUT | EPOLLET`), with one @rc{Executor} per worker thread.
- `SO_REUSEPORT` multi-threaded listener binding — kernel-level connection distribution across threads, with no shared accept lock.
- C++20 coroutine scheduler using @rc{Task, Task<T>}; each connection is represented by a single coroutine for its lifetime.
- `io_uring` integration through @rc{IoUringInstance} for asynchronous file I/O, including @rc{FileReadAwaitable} and @rc{FileWriteAwaitable} with seek support.
- Raw TCP stream abstraction (@rnet{TcpStream}).
- TLS stream abstraction (@rnet{TlsStream}) via OpenSSL, templated alongside @rnet{TcpStream} so connection-handling code remains transport-agnostic.
- Graceful executor shutdown.

---

# Thread Pool
- General-purpose @rpool{ThreadPool} for offloading blocking work, currently used by the SQLite backend.
- Job abstraction (@rpool{IPoolJob}, @rpool{PoolJob}) with a @rpool{PoolJobAwaitable} bridge back into the coroutine world.
- Configurable maximum queue size.

---

# HTTP

## Server & Connection Handling
- @rhttp{HttpServer}: TLS context setup, multiple listeners through @rhttp{HttpServer::addListener, addListener} and @rhttp{HttpServer::addTlsListener, addTlsListener}, configurable listen backlog, and @rhttp{HttpServer::run, run(N)} worker threads.
- @rhttp{HttpConnection}: per-connection request/response loop templated over the stream type, supporting both plain and TLS connections.
- Global connection limiting (`ConnGuard`) — bounded concurrent connections with RST on overflow instead of unbounded queuing.
- Bounded write buffer.
- Keep-alive request loop with connection reuse.

## Request Parsing
- Request-line parsing for method, path, and version, including fragment stripping and `Host` header parsing.
- Case-insensitive, multi-value-capable @rhttp{HeaderStore}: @rhttp{HeaderStore::getHeader,getHeader} returns the last match, @rhttp{HeaderStore::getHeaders,getHeaders} returns all values, @rhttp{HeaderStore::addHeader,addHeader} appends, and @rhttp{HeaderStore::setHeader,setHeader} replaces.
- Query-string parsing: `getQueryParam` for one value, `getQueryParams` for multiple values, and `getAllQueryParams` for the full pair list.
- Path-parameter extraction (`<param>` segments) through the router, exposed on the request object.
- `Content-Length` parsing with an explicit `ContentLengthError` rather than silent fallback.
- `Range` header parsing into a list of byte ranges through `getRanges()`, including multiple ranges per request.
- Cookie parsing through `getCookies` and `getCookie`.
- Request attributes: a generic string key/value bag for middleware to attach data to a request using `setAttribute` and `getAttribute`.
- Client IP and port capture.

## Request Bodies
- @rhttp{BodyStream} — lazy, pull-based body reading; the body is not eagerly read into memory before the handler runs.
- Chunked transfer-encoding decoder (@rhttp{ChunkDecoder}) implemented as a state machine: `CHUNK_SIZE → CHUNK_BODY → CHUNK_CRLF → TRAILER → DONE`, with explicit `MALFORMED`, `CHUNK_TOO_LARGE`, and `REQUEST_SIZE_LIMIT_EXCEEDED` error states.
- `multipart/form-data` parsing through @rhttp{MultipartParser}, using streaming buffered reads rather than a full-body-in-memory parse.
- URL-encoded form body support.
- JSON body support through `nlohmann::json` at the application layer.

## Responses
- @rhttp{HttpResponse} with status code, body, content type, and explicit handling for no-body status codes according to the HTTP specification.
- @rhttp{HttpStreamResponse} for generator-style streaming responses; handlers yield chunks through a lambda returning `Task<std::optional<std::string>>`, with `std::nullopt` terminating the stream.
- Automatic switching from chunked encoding to `Content-Length` for static file streaming when the size is known in advance.
- Range responses: `206 Partial Content` for single and multipart ranges, `416 Range Not Satisfiable`, and `If-Range` support backed by seekable asynchronous file reads.
- Response compression using gzip and Brotli, selected through content negotiation against `Accept-Encoding` q-values (@rcomp{CompressorFactory}, @rcomp{ICompressor}).
- MIME type resolution through @rhttp{MimeTypes} and a defined set of compressible MIME types.
- Content negotiation for error responses so the error representation matches a format accepted by the client.

## Routing
- Trie-based @rhttp{Router}, avoiding a linear scan over registered routes.
- Verb-specific registration: `get`, `post`, `put`, `patch`, and `delete_`.
- Path parameters such as `/users/<id>`.
- Single-segment wildcards such as `/files/*`.
- Deep wildcards of arbitrary depth such as `/files/**`.
- `405 Method Not Allowed` with a correctly computed `Allow` header through `getAllowedMethodsString`.
- Route-pattern validation at registration time.

## Middleware
- Express-style middleware chain through `router.use(...)`; middleware runs in registration order and calls `next()` to continue.
- @rmw{CorsMiddleware} with configurable allowed origins and max-age.
- @rmw{StaticMiddleware} for directory-backed file serving with its own caching and compression handling; a hit short-circuits the remaining chain.
- @rmw{CompressionMiddleware} for uniform gzip/Brotli negotiation across handler responses.
- @rmw{CacheControlMiddleware} with per-route pattern rules, per-MIME-type rules, and a default fallback.
- @rmw{SessionMiddleware} for attaching session state to requests.

## Cookies & Sessions
- @rhttp{Cookie} type with standard attributes.
- @rhttp{CookieStore} for reading and writing cookies on requests and responses.
- Session abstraction (@rhttp{Session}, @rhttp{SessionHandle}) decoupled from storage through @rhttp{ISessionStore}.
- @rhttp{InMemorySessionStore} with TTL-based in-memory storage.

## Errors
- Centralized @rhttp{ErrorFactory} for consistent error responses across the router, middleware, and handlers.

---

# Logging
- Asynchronous spdlog logger with a dedicated background thread and a bounded 8192-slot queue using a block-on-overflow policy.
- `flush_on(warn)` plus a periodic three-second flush.
- Configurable colored sinks: off, console, file, or both.

---

# Database Connectivity
- @rdb{IDatabase} backend-agnostic interface; the ORM and application code depend on this abstraction rather than directly on SQLite.
- @rdb{ITransaction} interface with @rdb{ScopedTransaction} and @rdb{Sqlite3Transaction} implementations.
- @rdb{Sqlite3Db} as the current concrete database implementation, using WAL mode.
- Foreign-key enforcement enabled through `PRAGMA foreign_keys` at the connection level.
- `ConnectionQueue` connection pool: fixed-size `sqlite3*` pool, condition-variable-gated acquisition/release, and a guarantee that a connection is not touched by two threads simultaneously.
- Per-connection cached prepared statements through `Connection::statements`, keyed by SQL text.
- `StatementResetGuard` for RAII reset/clear-bindings on `sqlite3_stmt`.
- Blocking SQLite calls routed through the shared @rpool{ThreadPool}.
- Structured `DatabaseError` / `DbErrorType` categories rather than exposing only raw SQLite error codes or strings.
- SQLite is compiled as amalgamation source directly into the `rukh` static library, so no external SQLite installation is required.

---

# ORM

## Records & Schema
- @rorm{ActiveRecord}<Model, PkTypes...> base class; models are plain structs with no macros or code generation.
- Columns declared as `static constexpr` data using @rorm{Column}{...}, including field pointer, database column name, primary-key flag, and related metadata.
- Composite primary keys through the variadic `PkTypes...` parameter pack.
- Nullable columns through `std::optional<T>` fields.
- Custom scalar conversion hooks through `toDbValueImpl` / `fromDbValueImpl`.
- Check constraints through `checkConstraint` and `CheckPredicate`, declared alongside columns.

## CRUD
- `find(pk)`, `save()` (insert-or-update based on record state), and `destroy()` on individual records.
- `bulkInsert`, `bulkUpdate`, and related bulk variants returning affected counts and optionally affected rows.
- `bulkDestroy`.
- `getOne`, `getOneOptional`, `first`, `exists`, and `count` on queries.
- `getOneOrCreate` for atomic find-or-insert; this currently needs improvement once savepoints are added.
- Query-returning operations use `std::expected<T, DatabaseError>`, avoiding throw-on-not-found behavior and silent null results.

## Query Builder
- @rorm{SelectQuery}, @rorm{InsertQuery}, @rorm{UpdateQuery}, @rorm{DeleteQuery}, and @rorm{UpsertQuery}, sharing a common @rorm{WhereClause} / @rorm{QueryDispatcher} base.
- Typed @rorm{Predicate}<Model> conditions for `WHERE`, including comparison operators and helpers, avoiding hand-built condition SQL.
- `join()` with inner, left, right, full, and cross joins, with optional table aliasing.
- `groupBy()` / `having()`.
- `orderBy()` by column name or typed field pointer, ascending or descending.
- `distinct()`.
- `limit()` / `offset()`, with `clearLimit()` / `clearOffset()`.
- Set operations: `UNION`, `UNION ALL`, `INTERSECT`, `INTERSECT ALL`, `EXCEPT`, and `EXCEPT ALL`.
- Common Table Expressions through `withCte`, including non-recursive and recursive CTEs.
- Raw `execute()` escape hatch for cases not covered by the builder.

## Relations
- Many-to-one (@rorm{ManyToOneRelation}) and one-to-one (@rorm{OneToOneRelation}) foreign-key relations.
- Per-relation `OnDelete` policies: `CASCADE`, `NO_ACTION`, `RESTRICT`, `SET_DEFAULT`, and `SET_NULL`, with `static_assert` checks where policy and column nullability are incompatible.
- Many-to-many relations through @rorm{ManyToManyRelation}, including self-referential relations.
- Four M2M symmetry modes: `SINGLE_ROW`, `DB_SINGLE_ROW`, `DOUBLE_ROW`, and `DB_DOUBLE_ROW`.
- Custom through-models for M2M relations, with @rorm{DefaultThroughModel} available when a custom model is unnecessary.
- Named relations using `FixedString` NTTPs through `.withRelationName<"friendship">()` and reciprocal names through `.withReciprocalName<"followers">()`.
- Relation traversal through `ref()`, `related()`, and `manyRelated()`, with compile-time validation of relation definitions.

## Hydration
- Tuple-based row hydration through `std::apply`.
- Column hydration by name rather than position, so reordering `SELECT` columns does not break mapping.
- Joined and multi-model row hydration for `join()` queries.
- Optional-type unwrapping during hydration so SQL `NULL` maps cleanly to `std::nullopt`.
- Hydration failures identify the specific column that failed.

## Transactions
- @rdb{ScopedTransaction} with semi-RAII rollback semantics.
- @rdb{Sqlite3Transaction}.
