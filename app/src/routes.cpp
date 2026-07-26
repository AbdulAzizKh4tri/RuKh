#include <cstdlib>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <rukh/AsyncFileReader.hpp>
#include <rukh/AsyncFileWriter.hpp>
#include <rukh/ErrorFactory.hpp>
#include <rukh/HttpResponse.hpp>
#include <rukh/MultipartParser.hpp>
#include <rukh/ThreadPool.hpp>

#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/SelectQuery.hpp>

#include "models/User.hpp"
#include "routes.hpp"

using json = nlohmann::json;
using namespace rukh;

void registerRoutes(Router &router, const ErrorFactory &errorFactory, ThreadPool *threadPool, db::IDatabase *db) {

  models::User::db = db;

  router.get("/", [&errorFactory](const HttpRequest &request) -> Task<Response> {
    auto name = request.getQueryParam("name");

    HttpResponse response(200);
    response.headers.setHeaderLower("content-type", "text/html");
    if (request.getMethod() == "HEAD")
      response.stripBody();
    co_return response;
  });

  router.post("/", [](HttpRequest &request) -> Task<Response> {
    json data = json::parse(co_await request.consumeBody());
    auto res = HttpResponse(200, "Hello, " + std::string(data["name"]) + "!");
    res.headers.setHeaderLower("content-type", "text/plain");
    co_return res;
  });

  router.put(
      "/", [](HttpRequest &request) -> Task<Response> { co_return HttpResponse(200, co_await request.consumeBody()); });

  // WARNING: BLOCKS THE ENTIRE THREAD FOR 5 SECONDS!
  router.get("/tests/slow", [](const HttpRequest &) -> Task<Response> {
    // simulate slow work -- busy wait for 5 seconds
    auto end = now() + std::chrono::seconds(5);
    while (now() < end) {
    }
    co_return HttpResponse(200, "slow response");
  });

  router.get("/tests/slowpool", [threadPool](const HttpRequest &) -> Task<Response> {
    co_await threadPool->submit([]() {
      auto end = now() + std::chrono::seconds(5);
      while (now() < end) {
      }
      return 0;
    });
    co_return HttpResponse(200, "slow pooled response");
  });

  // -- Test routes ------------------------------------------------------------
  // All live under /tests/* so they're instantly readable in the server log.

  // GET /tests/ping
  // Simplest possible liveness check.
  router.get("/tests/ping", [](const HttpRequest &) -> Task<Response> {
    co_return HttpResponse(200, "text/html; charset=utf-8", "pong");
  });

  // GET|POST /tests/debug/request
  // Full request introspection -- returns every observable property of the
  // incoming request as a single JSON object. Replaces the old per-concern
  // routes (GET /tests/echo, /tests/headers, /tests/decode/query,
  // /tests/decode/rawpath) which scattered the same information across four
  // endpoints. Use this as the single source of truth for header, query,
  // cookie, path, and body tests.
  //
  // Response shape:
  // {
  //   "method":  "GET",
  //   "path":    "/tests/debug/request",
  //   "rawPath": "/tests/debug/request?foo=bar",
  //   "headers": { "host": "localhost:8080", ... },
  //   "query":   { "foo": "bar" },
  //   "cookies": { "session_id": "abc123" },
  //   "body":    ""
  // }
  auto debugRequestHandler = [](HttpRequest &request) -> Task<Response> {
    json cookies = json::object();
    for (const auto &[name, value] : request.getCookies())
      cookies[name] = value;

    const std::string body = co_await request.consumeBody();

    json j = {
        {"method", request.getMethod()},
        {"path", request.getPath()},
        {"rawPath", request.getRawPath()},
        {"headers", toJsonObject(request.getAllHeaders())},
        {"query", toJsonObject(request.getAllQueryParams())},
        {"cookies", cookies},
        {"body", body},
    };
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  };

  router.get("/tests/debug/request", debugRequestHandler);
  router.post("/tests/debug/request", debugRequestHandler);

  // POST /tests/echo
  // Echoes the raw request body back verbatim and mirrors the Content-Type.
  // Kept because body-echo tests (empty body, near-limit, chunked) need a
  // route that returns the body directly, not wrapped in JSON.
  router.post("/tests/echo", [](HttpRequest &request) -> Task<Response> {
    auto res = HttpResponse(200, co_await request.consumeBody());
    auto ct = request.getHeader("Content-Type");
    if (not ct.empty())
      res.headers.setHeaderLower("content-type", std::string(ct));
    co_return res;
  });

  // PUT /tests/echo
  // Same as POST echo -- useful for testing PUT-specific behaviour (405 etc.).
  router.put("/tests/echo", [](HttpRequest &request) -> Task<Response> {
    auto res = HttpResponse(200, co_await request.consumeBody());
    auto ct = request.getHeader("Content-Type");
    if (not ct.empty())
      res.headers.setHeaderLower("content-type", std::string(ct));
    co_return res;
  });

  // GET /tests/error/throw
  // Deliberately throws a std::runtime_error to exercise the exception handler
  // in HttpConnection::generateResponse() -- should produce a 500 JSON
  // response with Connection: keep-alive (handler exceptions are non-fatal).
  router.get("/tests/error/throw",
             [](const HttpRequest &) -> Task<Response> { throw std::runtime_error("Deliberate test error"); });

  // -- Cookie routes ----------------------------------------------------------
  // All live under /tests/cookies/*

  // GET /tests/cookies/set
  // Sets a cookie on the response, controlled by query params:
  //
  //   name=<str>     cookie name  (default: "test")
  //   value=<str>    cookie value (default: "hello")
  //   path=<str>     Path attribute   (optional, default: "/")
  //   domain=<str>   Domain attribute (optional)
  //   maxage=<int>   Max-Age in seconds (optional)
  //   samesite=<str> SameSite value: Strict | Lax | None (optional)
  //   httponly       if present (key-only), sets HttpOnly
  //   secure         if present (key-only), sets Secure
  //
  // Also responds with a JSON body describing the cookie that was set, so
  // tests can assert both the Set-Cookie response header and the attributes.
  router.get("/tests/cookies/set", [](const HttpRequest &request) -> Task<Response> {
    std::string name = request.getQueryParam("name");
    std::string value = request.getQueryParam("value");
    if (name.empty())
      name = "test";
    if (value.empty())
      value = "hello";

    Cookie c;
    c.name = name;
    c.value = value;

    auto path = request.getQueryParam("path");
    if (!path.empty())
      c.path = path;

    auto domain = request.getQueryParam("domain");
    if (!domain.empty())
      c.domain = domain;

    auto maxage = request.getQueryParam("maxage");
    if (!maxage.empty())
      c.maxAge = std::stoi(maxage);

    auto samesite = request.getQueryParam("samesite");
    if (!samesite.empty())
      c.sameSite = samesite;

    // key-only params arrive with value "true" -- presence is enough
    if (!request.getQueryParam("httponly").empty())
      c.httpOnly = true;
    if (!request.getQueryParam("secure").empty())
      c.secure = true;

    json body = {
        {"name", c.name},     {"value", c.value},       {"path", c.path},         {"domain", c.domain},
        {"maxAge", c.maxAge}, {"sameSite", c.sameSite}, {"httpOnly", c.httpOnly}, {"secure", c.secure},
    };

    HttpResponse res(200, body.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    res.cookies.setCookie(c);
    co_return res;
  });

  // GET /tests/cookies/read
  // Echoes all cookies from the incoming Cookie request header as a JSON
  // object. Use in tandem with /tests/cookies/set: set a cookie in one
  // request, then send it back here to confirm the round-trip.
  //
  // Response: { "<name>": "<value>", ... }
  router.get("/tests/cookies/read", [](const HttpRequest &request) -> Task<Response> {
    json j = json::object();
    for (const auto &[name, value] : request.getCookies()) {
      j[name] = value;
    }
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/cookies/delete
  // Instructs the client to remove a named cookie by issuing a Set-Cookie
  // header with Max-Age=0 via removeCookie().
  // Query param: name=<str> (default: "test")
  //
  // Response: { "removed": "<name>" }
  router.get("/tests/cookies/delete", [](const HttpRequest &request) -> Task<Response> {
    std::string name = request.getQueryParam("name");
    if (name.empty())
      name = "test";
    HttpResponse res(200, json{{"removed", name}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    res.cookies.deleteCookie(name);
    co_return res;
  });

  // -- Session routes ----------------------------------------------------------

  // GET /tests/session/set?key=foo&value=bar
  router.get("/tests/session/set", [](HttpRequest &request) -> Task<Response> {
    std::string key = request.getQueryParam("key");
    if (key.empty())
      key = "test";
    std::string value = request.getQueryParam("value");
    if (value.empty())
      value = "hello";

    auto session = co_await request.getSession();
    session->set(key, value);

    HttpResponse res(200, json{{"key", key}, {"value", value}}.dump());
    co_return res;
  });

  // GET /tests/session/get?key=foo
  router.get("/tests/session/get", [](HttpRequest &request) -> Task<Response> {
    std::string key = request.getQueryParam("key");
    if (key.empty())
      key = "test";

    auto session = co_await request.getSession();
    auto val = session->get(key);

    json body;
    body["key"] = key;
    if (val.has_value()) {
      body["value"] = *val;
      body["found"] = true;
    } else {
      body["value"] = nullptr;
      body["found"] = false;
    }

    HttpResponse res(200, body.dump());
    co_return res;
  });

  // GET /tests/session/all
  router.get("/tests/session/all", [](HttpRequest &request) -> Task<Response> {
    auto session = co_await request.getSession();
    json body = json::object();
    for (const auto &[k, v] : session->getAll()) {
      body[k] = v;
    }
    HttpResponse res(200, body.dump());
    co_return res;
  });

  // GET /tests/session/delete?key=foo
  router.get("/tests/session/delete", [](HttpRequest &request) -> Task<Response> {
    std::string key = request.getQueryParam("key");
    if (key.empty())
      key = "test";

    auto session = co_await request.getSession();
    bool existed = session->has(key);
    session->remove(key);

    HttpResponse res(200, json{{"removed", key}, {"existed", existed}}.dump());
    co_return res;
  });

  // GET /tests/session/invalidate
  router.get("/tests/session/invalidate", [](HttpRequest &request) -> Task<Response> {
    auto session = co_await request.getSession();
    session->invalidate();

    HttpResponse res(200, json{{"invalidated", true}}.dump());
    co_return res;
  });

  // -- Path parameter routes --------------------------------------------------

  // GET /tests/users/<id>
  router.get("/tests/users/<id>", [](const HttpRequest &request) -> Task<Response> {
    json j = {{"userId", request.getPathParam("id")}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  router.get("/tests/users/me/posts/", [](const HttpRequest &request) -> Task<Response> {
    json j = {{"user", "me"}, {"posts", {"post1", "post2", "post3"}}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/users/<userId>/posts/<postId>
  router.get("/tests/users/<userId>/posts/<postId>", [](const HttpRequest &request) -> Task<Response> {
    json j = {{"userId", request.getPathParam("userId")}, {"postId", request.getPathParam("postId")}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // DELETE /tests/items/<id>
  router.delete_("/tests/items/<id>",
                 [](const HttpRequest &request) -> Task<Response> { co_return HttpResponse(200); });

  // GET /tests/wildcard/* -- single segment
  router.get("/tests/wildcard/*", [](const HttpRequest &request) -> Task<Response> {
    json j = {{"path", request.getPathParam("*")}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/deepwildcard/** -- greedy
  router.get("/tests/deepwildcard/**", [](const HttpRequest &request) -> Task<Response> {
    json j = {{"path", request.getPathParam("**")}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // -- URL decoding test routes -----------------------------------------------

  // GET /tests/decode/path/<name>
  // Returns the percent-decoded path segment under the key "name".
  // Query and raw-path decode tests use /tests/debug/request instead, which
  // exposes both "query" and "rawPath" fields directly.
  //
  // e.g. /tests/decode/path/John%20Doe  ->  {"name":"John Doe"}
  // e.g. /tests/decode/path/foo%2Fbar   ->  {"name":"foo/bar"}
  // e.g. /tests/decode/path/caf%C3%A9   ->  {"name":"cafe"} (UTF-8 bytes)
  router.get("/tests/decode/path/<name>", [](const HttpRequest &request) -> Task<Response> {
    json j = {{"name", request.getPathParam("name")}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // -- Chunked / streaming response test routes -------------------------------
  // All live under /tests/stream/*

  // GET /tests/stream/basic
  // A handful of small named chunks. Assembled body: "Hello, World!"
  // The canonical "does streaming work at all" check.
  router.get("/tests/stream/basic", [](const HttpRequest &) -> Task<Response> {
    co_return HttpStreamResponse(200, "text/plain", [i = 0]() mutable -> Task<std::optional<std::string>> {
      static constexpr std::array chunks = {"Hello", ", ", "World", "!"};
      if (i >= 4)
        co_return std::nullopt;
      co_return std::string(chunks[i++]);
    });
  });

  // GET /tests/stream/single
  // Exactly one chunk, then done. Assembled body: "hello"
  router.get("/tests/stream/single", [](const HttpRequest &) -> Task<Response> {
    co_return HttpStreamResponse(200, [done = false]() mutable -> Task<std::optional<std::string>> {
      if (done)
        co_return std::nullopt;
      done = true;
      co_return std::string("hello");
    });
  });

  // GET /tests/stream/empty
  // Returns nullopt on the very first call -- terminal chunk immediately.
  // Assembled body is empty, status still 200.
  router.get("/tests/stream/empty", [](const HttpRequest &) -> Task<Response> {
    co_return HttpStreamResponse(200, []() -> Task<std::optional<std::string>> { co_return std::nullopt; });
  });

  // GET /tests/stream/count/<n>
  // Streams exactly n chunks: "chunk-1", "chunk-2", ..., "chunk-n".
  // n=0  -> empty body (same as /empty)
  // n<0  -> clamped to 0
  // non-numeric n -> std::stoi throws before the HttpStreamResponse is
  //   constructed, generateResponse() catches it -> plain 500 JSON response.
  router.get("/tests/stream/count/<n>", [](const HttpRequest &request) -> Task<Response> {
    int n = std::stoi(request.getPathParam("n"));
    if (n < 0)
      n = 0;
    co_return HttpStreamResponse(200, [i = 0, n]() mutable -> Task<std::optional<std::string>> {
      if (i >= n)
        co_return std::nullopt;
      co_return "chunk-" + std::to_string(++i);
    });
  });

  // GET /tests/stream/throw
  // Emits two chunks successfully, then the lambda throws.
  // The error is caught, "Internal Server Error: ..." is appended as a final
  // chunk, the stream is terminated, and the connection closes cleanly.
  // Status is still 200 (headers already sent).
  // Assembled body: "chunk-1chunk-2Internal Server Error: Deliberate stream
  // error"
  router.get("/tests/stream/throw", [](const HttpRequest &) -> Task<Response> {
    co_return HttpStreamResponse(200, [i = 0]() mutable -> Task<std::optional<std::string>> {
      if (i == 2)
        throw std::runtime_error("Deliberate stream error");
      co_return "chunk-" + std::to_string(++i);
    });
  });

  // POST /tests/stream/echo
  // Reads the request body and streams it back in 4-byte chunks.
  // Empty body -> empty stream (terminal chunk only).
  // Mirrors Content-Type if provided.
  router.post("/tests/stream/echo", [](HttpRequest &request) -> Task<Response> {
    std::string body = co_await request.consumeBody();
    auto ct = request.getHeader("Content-Type");
    auto res = HttpStreamResponse(
        200, [offset = size_t(0), body = std::move(body)]() mutable -> Task<std::optional<std::string>> {
          if (offset >= body.size())
            co_return std::nullopt;
          auto len = std::min(size_t(4), body.size() - offset);
          auto chunk = body.substr(offset, len);
          offset += len;
          co_return chunk;
        });
    if (not ct.empty())
      res.headers.setHeaderLower("content-type", std::string(ct));
    co_return res;
  });

  // -- Thread pool test routes -------------------------------
  // All live under /tests/pool/*

  // GET /tests/pool/basic
  // Submits a task that returns a value, awaits it immediately.
  // Verifies: submit, return value propagation, basic await.
  // Response: { "result": 42 }
  router.get("/tests/pool/basic", [threadPool](const HttpRequest &) -> Task<Response> {
    int result = co_await threadPool->submit([]() { return 42; });
    auto res = HttpResponse(200, json{{"result", result}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/pool/exception
  // Submits a task that throws. Verifies exception propagates correctly
  // to the handler, gets caught by HttpConnection, returns 500.
  router.get("/tests/pool/exception", [threadPool](const HttpRequest &) -> Task<Response> {
    int result = co_await threadPool->submit([]() -> int {
      throw std::runtime_error("pool task exploded");
      return 0;
    });
    (void)result;
    co_return HttpResponse(200, "unreachable");
  });

  // GET /tests/pool/deferred
  // Submits a task, logs that it's been submitted (proving coroutine
  // continued past submit before awaiting), then awaits the result.
  // Verifies: deferred await, return value used in response.
  // Response: { "result": 99, "note": "task was submitted before this line ran" }
  router.get("/tests/pool/deferred", [threadPool](const HttpRequest &) -> Task<Response> {
    auto handle = threadPool->submit([]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      SPDLOG_INFO("TASK DONE");
      return 99;
    });

    co_await threadPool->submit([]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      return;
    });

    SPDLOG_INFO("co_await-ing now");
    int result = co_await handle;
    auto res = HttpResponse(200, json{{"result", result}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/pool/concurrent
  // Fires n tasks simultaneously, awaits all of them.
  // Each task sleeps for 500ms and returns its index.
  // Total wall time should be ~500ms regardless of n (up to pool size).
  // Verifies: actual parallelism.
  // Response: { "results": [0, 1, 2], "note": "should take ~500ms not ~1500ms" }
  router.get("/tests/pool/concurrent", [threadPool](const HttpRequest &request) -> Task<Response> {
    int n = 3;
    auto start = now();
    auto nParam = request.getQueryParam("n");
    if (!nParam.empty())
      n = std::clamp(std::stoi(nParam), 1, 16);

    std::vector<PoolTaskAwaitable<int>> handles;
    handles.reserve(n);

    for (int i = 0; i < n; i++) {
      handles.push_back(threadPool->submit([i]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return i;
      }));
    }

    json results = json::array();
    for (auto &handle : handles)
      results.push_back(co_await handle);

    auto end = now();

    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    auto res = HttpResponse(200, json{{"results", results}, {"time", time}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/pool/forget
  // Fires a task with fireAndForget — no await, no result.
  // Task logs a message server-side after 200ms.
  // Verifies: fire and forget doesn't crash, response returns immediately.
  // Response: { "note": "task is running in background, check server logs" }
  router.get("/tests/pool/forget", [threadPool](const HttpRequest &) -> Task<Response> {
    threadPool->fireAndForget([]() noexcept {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      SPDLOG_INFO("fireAndForget task completed");
    });

    auto res = HttpResponse(200, json{{"note", "task is running in background, check server logs"}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/pool/unawaited
  // Constructs a PoolAwaitable via submit() but never co_awaits it.
  // Response: 200 immediately.
  router.get("/tests/pool/unawaited", [threadPool](const HttpRequest &) -> Task<Response> {
    {
      auto handle = threadPool->submit([]() {
        SPDLOG_INFO("Submitted task running, there should be a warning for not awaiting it");
        return 0;
      });
    }
    co_return HttpResponse(200, "no crash, check logs for unawaited warning");
  });

  // POST /tests/forms/urlencoded
  // Accepts URL-encoded form data, returns it as JSON.
  // Response: { "username": "alice", "password": "secret" }
  router.post("/tests/forms/urlencoded", [](HttpRequest &request) -> Task<Response> {
    auto formData = co_await request.getFormData();
    auto res =
        HttpResponse(200, json{{"username", formData["username"][0]}, {"password", formData["password"][0]}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // POST /tests/forms/urlencoded/checkboxes
  // Accepts URL-encoded form data, returns it as JSON.
  // Response: { "username": "alice", "password": "secret", "terms": ["1", "2", "3"] }
  router.post("/tests/forms/urlencoded/checkboxes", [](HttpRequest &request) -> Task<Response> {
    auto formData = co_await request.getFormData();
    auto terms = formData["terms"];
    auto res = HttpResponse(
        200,
        json{{"username", formData["username"][0]}, {"password", formData["password"][0]}, {"terms", terms}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // POST /tests/forms/json
  // Accepts JSON form data, returns it as JSON.
  // Response: { "username": "alice", "password": "secret" }
  router.post("/tests/forms/json", [](HttpRequest &request) -> Task<Response> {
    auto body = co_await request.jsonBody();
    auto res = HttpResponse(200, json{{"username", body["username"]}, {"password", body["password"]}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // POST /tests/forms/multipart
  // Accepts multipart form data, returns it as JSON.
  // Response: { "username": "alice", "password": "secret" }
  router.post("/tests/forms/multipart", [](HttpRequest &request) -> Task<Response> {
    MultipartParser mp(request);
    std::string username, password;
    std::vector<std::string> terms;
    mp.onField("username", [&username](std::string v) -> Task<void> {
      username = v;
      co_return;
    });
    mp.storeFieldValue("password", password);
    mp.storeFieldValues("terms", terms);

    std::optional<AsyncFileWriter> writerOpt = AsyncFileWriter::open("./public/test.bin");
    if (not writerOpt)
      throw std::runtime_error("File issue");
    mp.onFile("file", [&writerOpt](std::span<unsigned char> data) -> Task<void> {
      co_await writerOpt->writeChunk(std::string_view(reinterpret_cast<char *>(data.data()), data.size()));
    });

    co_await mp.go();

    HttpResponse res(200, json{{"username", username}, {"password", password}, {"terms", terms}}.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // -- Database test routes -------------------------------
  // All live under /tests/db/* and interact with the 'users' table
  // Schema: id (INTEGER PRIMARY KEY), name (TEXT NOT NULL), email (TEXT),
  //         age (INTEGER), created_at (DATETIME DEFAULT CURRENT_TIMESTAMP)

  // GET /tests/db/select
  // Queries users table with optional filtering parameters
  // Query params:
  //   - name: Filter users by name (case-insensitive substring match)
  //   - email: Filter users by email (exact match)
  //   - minAge: Filter users with age >= minAge
  //   - maxAge: Filter users with age <= maxAge
  //   - orderBy: Sort results (name, email, age, created_at; default: name)
  router.get("/tests/db/select", [threadPool, db](const HttpRequest &request) -> Task<Response> {
    std::string nameFilter = request.getQueryParam("name");
    std::string emailFilter = request.getQueryParam("email");
    std::string minAgeStr = request.getQueryParam("minAge");
    std::string maxAgeStr = request.getQueryParam("maxAge");
    std::string orderBy = request.getQueryParam("orderBy");

    auto query = models::User::all();
    if (not nameFilter.empty())
      query.where({"name", "like", "%" + request.getQueryParam("name") + "%"});
    if (not emailFilter.empty())
      query.andWhere({"email", "=", request.getQueryParam("email")});
    if (not minAgeStr.empty())
      query.andWhere({"age", ">=", request.getQueryParam("minAge")});
    if (not maxAgeStr.empty())
      query.andWhere({"age", "<=", request.getQueryParam("maxAge")});
    if (not orderBy.empty())
      query.orderBy(orderBy);

    // Execute query in thread pool to avoid blocking the event loop
    auto users = co_await threadPool->submit(
        [db, &query]() -> std::optional<std::vector<models::User>> { return query.get(db); });

    if (not users) {
      auto res = HttpResponse(500, json{{"error", "Database error"}}.dump());
      res.headers.setHeaderLower("content-type", "application/json");
      co_return res;
    }

    json arr = json::array();
    for (auto &user : *users) {
      arr.push_back(user.toJson());
      SPDLOG_INFO("User: {}", user.toString());
    }

    // Build response as array of user objects
    co_return HttpResponse(200, "application/json", arr.dump());
  });

  // POST /tests/db/insert
  // Inserts a new user record into the users table
  // Required JSON fields:
  //   - name: User's full name (string)
  // Optional JSON fields:
  //   - email: User's email address (string)
  //   - age: User's age (integer)
  router.post("/tests/db/insert", [threadPool, db](HttpRequest &request) -> Task<Response> {
    auto body = co_await request.jsonBody();
    if (body.is_discarded())
      co_return HttpResponse(400, "application/json", json{{"error", "invalid JSON"}}.dump());

    // Validate required 'name' field
    if (!body.contains("name") || !body["name"].is_string())
      co_return HttpResponse(400, "application/json", json{{"error", "name is required"}}.dump());

    models::User user;
    user.name = body["name"].get<std::string>();
    user.email = body.contains("email") ? body["email"].get<std::string>() : "";
    user.age = body.contains("age") ? body["age"].get<int64_t>() : 0;
    user.password = body.contains("password") ? body["password"].get<std::string>() : "";

    // Execute insert in thread pool to avoid blocking the event loop
    auto result = co_await threadPool->submit([db, &user]() -> bool { return user.save(); });

    if (not result)
      co_return HttpResponse(500, "application/json", json{{"error", "Insert failed"}}.dump());

    co_return HttpResponse(201, "application/json", json{{"created", "Insert successful"}}.dump());
  });

  router.post("/tests/db/update", [threadPool, db](HttpRequest &request) -> Task<Response> {
    auto body = co_await request.jsonBody();
    if (body.is_discarded())
      co_return HttpResponse(400, "application/json", json{{"error", "invalid JSON"}}.dump());

    if (not body.contains("id"))
      co_return HttpResponse(400, "application/json", json{{"error", "id is required"}}.dump());

    auto id = body["id"].get<models::User::pk>();

    auto userOpt =
        co_await threadPool->submit([db, id]() -> std::optional<models::User> { return models::User::find(id); });
    if (not userOpt) {
      co_return HttpResponse(404, "application/json", json{{"error", "User not found"}}.dump());
    }
    auto user = *userOpt;

    user.name = body.contains("name") ? body["name"].get<std::string>() : "";
    user.email = body.contains("email") ? body["email"].get<std::string>() : "";
    user.age = body.contains("age") ? body["age"].get<int64_t>() : 0;
    user.password = body.contains("password") ? body["password"].get<std::string>() : "";

    // Execute update in thread pool to avoid blocking the event loop
    auto result = co_await threadPool->submit([db, &user]() -> bool { return user.save(); });

    if (not result)
      co_return HttpResponse(500, "application/json", json{{"error", "Update failed"}}.dump());

    co_return HttpResponse(200, "application/json", json{{"updated", "Update successful"}}.dump());
  });

  router.post("/tests/db/delete", [threadPool, db](HttpRequest &request) -> Task<Response> {
    auto body = co_await request.jsonBody();
    if (body.is_discarded())
      co_return HttpResponse(400, "application/json", json{{"error", "invalid JSON"}}.dump());

    if (not body.contains("id"))
      co_return HttpResponse(400, "application/json", json{{"error", "id is required"}}.dump());

    auto id = body["id"].get<models::User::pk>();
    auto user =
        co_await threadPool->submit([db, id]() -> std::optional<models::User> { return models::User::find(id); });
    if (not user) {
      co_return HttpResponse(404, "application/json", json{{"error", "User not found"}}.dump());
    }

    if (not user->destroy()) {
      co_return HttpResponse(500, "application/json", json{{"error", "Delete failed"}}.dump());
    }

    co_return HttpResponse(200, "application/json", json{{"deleted", "Delete successful"}}.dump());
  });
}
