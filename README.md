<p align="center">
  <img src="./logo.png" alt="RuKh logo, a RuKh (Roc), the mythical bird" width="400">
</p>

<p align="center">A C++23 web framework, built entirely from scratch on Linux.</p>

[![Docs](https://img.shields.io/badge/docs-online-blue)](https://abdulazizkh4tri.github.io/RuKh/)
---

RuKh started as an attempt to write an HTTP/1.1 server from raw sockets. It grew into a full web framework, every layer between the kernel and the handler function is hand written. The only dependencies being OpenSSL, spdlog, nlohmann Json, and liburing.

This is a solo learning project with a production quality bar. It's benchmarked against Drogon (fastest I could find for C++) and some other frameworks.

## What's actually built here

**Networking & runtime**
- Sockets, TCP streams, and TLS streams (OpenSSL) with non-blocking I/O
- An edge triggered `epoll` event loop, one `Executor` per worker thread
- A custom C++20 coroutine scheduler, hand written `Task<T>` return types and awaitables
- `SO_REUSEPORT` multi-threaded listener binding, with the kernel distributing connections across threads (May change in the future)
- Full connection lifecycle management: accept, TLS handshake, keep-alive loop, timeouts, and a global connection limit with RST on overflow instead of unbounded queuing
- `io_uring` for async file I/O, so disk reads never block a worker thread

**HTTP layer**
- Request parsing and case-insensitive, multi-value header storage
- A trie-based router, path parameters, single-segment and deep wildcards, correct `405`/`Allow` handling
- An Express-style middleware chain (`use()`, `next()`)
- Body parsing: `multipart/form-data`, URL-encoded forms, and JSON bodies, all via lazy pull-based body streaming rather than buffering the whole request up front
- Three response types: `HttpResponse` for buffered bodies, `HttpFileResponse` for file responses, and `HttpStreamResponse` for generator-style streaming
- File serving split by size: `sendfile()` for anything above a size threshold, buffered reads with caching for small files
- Range requests (`206`/`416`, multipart ranges, `If-Range`)
- gzip and Brotli compression via content negotiation, plus a `CacheControlMiddleware` for per-route/per-MIME caching rules
- Cookies and server-side sessions with a pluggable session store

**Database**
- A backend-agnostic, coroutine-based async database interface, with a SQLite3 implementation backed by a real connection pool and cached prepared statements
- An ORM sits on top of this today, but it's getting a full redo, not documented here until that's done

**Planned:** WebSockets and Server-Sent Events.

## Benchmarks

All frameworks were benchmarked against the same set of endpoints, with configurations matched as closely as each framework allows (same SQLite pragmas, same static file sizes, same middlewares where applicable). Every test ran on a single machine (my laptop, plugged-in) with the `wrk` client and the server process pinned to separate physical cores via `taskset`, release builds, logging disabled. Express is tested at 1 core; everything else is tested up to 3 server cores. I include go, express and starlette even though their selling point isn't performance,just so we have a reference with some popular frameworks in other languages.

**Workloads:**
- **plaintext**, `GET /ping`, no DB, no business logic; measures the raw request/response path
- **static_small / static_medium / static_large**, static file serving at increasing file sizes, to see where each framework's I/O strategy starts to matter
- **crud_mixed**, reads only (single-record fetch + list fetch) through each framework's DB layer. Writes were originally included too, but insert/update throughput turned out to be unreliable enough, even for the same framework run to run, that it wasn't a fair signal, so it was dropped from this workload. RuKh uses it's `IDatabase` interface and `QueryResult`, not the unfinished ORM.
- **churn**, one request per connection (`Connection: close`), no keep-alive; measures pure connection setup/teardown cost rather than steady-state throughput

**Results below are averaged across the full concurrency sweep, per framework at 3 cores.**
*concurrency (wrk -c) values were \[64,128,256,512,1024]*

#### Plaintext
| Framework | avg req/s | avg p99 latency |
|---|---|---|
| Drogon | 247,000 | 3.2 ms |
| **RuKh** | **223,000** | **2.2 ms** |
| Fiber | 217,000 | 3.3 ms |
| Crow | 143,000 | 3.1 ms |
| Go net/http | 142,000 | 6.4 ms |
| Starlette | 25,400 | 84.7 ms |
| Express (1 core) | 20,700 | 120.1 ms |

#### Static files (small / medium / large)
| Framework | small avg req/s | medium avg req/s | large avg req/s |
|---|---|---|---|
| Drogon | 149,000 | 9,725 | 137 |
| **RuKh** | **142,500** | 9,725 | 131 |
| Fiber | 109,000 | 9,708 | 131 |
| Go net/http | 54,100 | 9,690 | 132 |
| Crow | 9,500 | 2,953 | 64 |
| Express | 7,600 | 633 | 5 |
| Starlette | 3,200 | 813 | 10 |

RuKh trails Drogon slightly on small files and is essentially tied on medium and large.

#### Database reads (list and fetch)
| Framework | avg req/s | avg p99 latency |
|---|---|---|
| Go net/http | 17,163 | 100.3 ms |
| Fiber | 16,406 | 117.5 ms |
| **RuKh** | **14,564** | **32.4 ms** |
| Drogon | 12,421 | 51.5 ms |
| Express (1 core) | 6,143 | 117.7 ms |
| Crow | 5,971 | 63.4 ms |
| Starlette | 3,340 | 174.8 ms |

RuKh isn't the highest throughput here, but its p99 latency is significantly lower, Go and Fiber push more requests through but with far worse tail behavior.

#### Connection churn
| Framework | avg req/s | avg p99 latency |
|---|---|---|
| Crow | 44,270 | 5.4 ms |
| Fiber | 42,973 | 7.8 ms |
| Drogon | 42,224 | 8.8 ms |
| Go net/http | 41,515 | 10.4 ms |
| **RuKh** | **15,199** | **159.0 ms** |
| Starlette | 9,950 | 44.0 ms |
| Express (1 core) | 8,637 | 143.3 ms |

This is RuKh's clear weak point. Every other multi-threaded framework tested (except starlette) handles a fresh connection per request roughly 3x better than RuKh does, and RuKh's tail latency degrades badly under it. This is also why active development is paused as of writing this ReadMe. Rather than keep prompting an LLM to guess at the fix (tried it, didn't work), I'm taking the time to read up on the networking/kernel internals involved in connection setup and teardown so I understand why it's this bad before touching the code again. 
