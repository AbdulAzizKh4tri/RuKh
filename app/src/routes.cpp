#include <cstdint>
#include <cstdlib>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <rukh/AsyncFileReader.hpp>
#include <rukh/AsyncFileWriter.hpp>
#include <rukh/ErrorFactory.hpp>
#include <rukh/HttpResponse.hpp>
#include <rukh/MultipartParser.hpp>
#include <rukh/ThreadPool.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/db/ScopedTransaction.hpp>
#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/DefaultThroughModel.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/SelectQuery.hpp>
#include <rukh/orm/UpdateQuery.hpp>

#include "TestRunner.hpp"
#include "models/Follow.hpp"
#include "models/Post.hpp"
#include "models/PostLikeUser.hpp"
#include "models/User.hpp"
#include "routes.hpp"

using json = nlohmann::json;
using namespace rukh;

void registerRoutes(Router &router, const ErrorFactory &errorFactory, ThreadPool *threadPool, db::IDatabase *db) {

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

  // GET /tests/db/single_test
  // Runs a fixed sequence of ORM checks. Each step is isolated: a failure records that
  // step as failed and the suite continues. Response is a JSON summary with a per-step
  // pass/fail + detail, and an overall `allPassed`.
  router.get("/tests/db/single_test", [threadPool, db](const HttpRequest &request) -> Task<Response> {
    using namespace models;
    using namespace testutil;
    using Pu = Predicate<User>;
    using Pp = Predicate<Post>;

    TestRunner runner;

    // --- 0. Start from a known-empty table so the suite is idempotent ---
    co_await runner.run("cleanup", [db]() -> Task<void> {
      unwrap(co_await User::bulkDestroy({true}), "bulkDestroy");
      auto count = unwrap(co_await User::all().count(), "count");
      expect(count == 0, "expected 0 rows after cleanup, got " + std::to_string(count));

      unwrap(co_await Post::bulkDestroy({true}), "bulkDestroy");
      count = unwrap(co_await Post::all().count(), "count");
      expect(count == 0, "expected 0 rows after cleanup, got " + std::to_string(count));
    });

    // --- 1. Single insert, including a nullable field left unset ---
    User greg;
    greg.name = "greg";
    greg.email = "greg@example.com";
    greg.password = "secret";

    co_await runner.run("single insert", [&greg]() -> Task<void> {
      // save() doesn't mutate `alice` in place — capture the returned, DB-hydrated model.
      unwrap(co_await greg.save(), "save");
      expect(greg.id > 0, "id not populated after insert");
    });

    // --- 2. Single find, verify nullable round-trips as empty ---
    co_await runner.run("single find", [&greg]() -> Task<void> {
      auto found = unwrap(co_await User::find(greg.id), "find");
      expect(found.has_value(), "find() returned nullopt");
      expect(not found->age.has_value(), "age should be null, got " + std::to_string(found->age.value_or(-1)));
      expect(found->name == "greg", "name mismatch: " + found->name.value_or("<unknown>"));
    });

    // --- 3. Single update: set a previously-null field, null out a previously-set one ---
    co_await runner.run("single update", [&greg]() -> Task<void> {
      greg.age = 20;
      greg.password = std::nullopt;
      unwrap(co_await greg.save(), "save");

      auto found = unwrap(co_await User::find(greg.id), "find");
      expect(found.has_value() && found->age == 20, "age not updated");
      expect(not found->password.has_value(), "password should be null after update");
    });

    // --- 4. Single destroy ---
    co_await runner.run("single destroy", [&greg]() -> Task<void> {
      unwrap(co_await greg.destroy(), "destroy");
      auto count = unwrap(co_await User::all().count(), "count");
      expect(count == 0, "expected 0 rows after destroy, got " + std::to_string(count));
    });

    // --- 5. Bulk insert ---
    co_await runner.run("bulk insert", []() -> Task<void> {
      std::vector<User> batch;
      batch.push_back({.name = "Alice", .email = "alice@example.com"});
      batch.push_back({.name = "Bob", .email = "bob@example.com"});
      batch.push_back({.name = "Joe", .email = "joe@example.com"});
      batch.push_back({.name = "John", .email = "john@example.com"});

      auto [insertedCount, insertedRows] = unwrap(co_await User::bulkInsert(batch), "bulkInsert");
      expect(insertedCount == 4, "expected 4 rows, got " + std::to_string(insertedCount));
    });

    // --- 6. Bulk update, verify it actually applied ---
    co_await runner.run("bulk update", []() -> Task<void> {
      User patch;
      patch.name = "megaJoe";
      patch.password = "1234";
      Pu pred = Pu("LOWER(name) LIKE LOWER(?) ESCAPE '\\'", '%' + db::dbValueToString("j") + '%');
      pred = pred and Pu(true);
      auto [updatedCount, updatedRows] =
          unwrap(co_await User::bulkUpdate(patch, std::tuple{&User::name, &User::password}, pred), "bulkUpdate");
      expect(updatedCount == 2, "expected 2 rows, got " + std::to_string(updatedCount));

      auto afterUpdate = unwrap(co_await User::filter(pred).select(), "get");
      for (auto &u : afterUpdate)
        expect(u.name == "megaJoe", "row id " + std::to_string(u.id) + " not updated");
    });

    // --- 7. Bulk destroy, verify table is empty again ---
    co_await runner.run("bulk destroy", []() -> Task<void> {
      auto [destroyedCount, destroyedRows] = unwrap(co_await User::bulkDestroy({true}), "bulkDestroy");
      expect(destroyedCount == 4, "expected 4 rows, got " + std::to_string(destroyedCount));
      auto count = unwrap(co_await User::all().count(), "count");
      expect(count == 0, "expected 0 rows, got " + std::to_string(count));
    });

    // --- 8. Transaction commit ---
    User bob;
    bob.name = "bob";
    bob.email = "bob@example.com";
    bob.age = 12;
    bob.password = "secret";

    co_await runner.run("transaction commit", [db, &bob]() -> Task<void> {
      db::ScopedTransaction transaction(db);
      co_await transaction.begin();
      unwrap(co_await bob.save(&transaction), "save");
      auto res = co_await transaction.commit();
      expect(res, "commit() returned false");

      auto found = unwrap(co_await User::find(bob.id), "find");
      expect(found.has_value() && found->age == 12, "expected age to be 12 after commit");
    });

    // --- 9. Transaction rollback: change visible inside txn, invisible outside, reverted after rollback ---
    co_await runner.run("transaction rollback", [db, &bob]() -> Task<void> {
      bob.age = 6;
      {
        db::ScopedTransaction transaction(db);
        co_await transaction.begin();
        // Deliberately not reassigning `bob` here — the outer variable must keep
        // representing the pre-transaction state for the "outside" check below.

        auto res = co_await bob.save(&transaction);
        unwrap(res, "save inside transaction");

        auto outside = unwrap(co_await User::find(bob.id), "find outside transaction");
        expect(outside.has_value() && outside->age == 12, "expected age to still be 12 outside the transaction");

        auto inside = unwrap(co_await User::find(bob.id, &transaction), "find inside transaction");
        expect(inside.has_value() && inside->age == 6, "expected age to be 6 inside the transaction");

        expect(co_await transaction.rollback(), "rollback() returned false");
      }

      auto found = unwrap(co_await User::find(bob.id), "find");
      expect(found.has_value() && found->age == 12, "expected age to be 12 due to rollback");
    });

    // --- 10. Insert-then-rollback: demonstrates the documented persisted_/id staleness caveat ---
    co_await runner.run("insert then rollback", [db]() -> Task<void> {
      User carol;
      carol.name = "carol";
      carol.email = "carol@example.com";
      carol.password = "secret";

      int64_t carolId = 0;
      {
        db::ScopedTransaction transaction(db);
        co_await transaction.begin();
        unwrap(co_await carol.save(&transaction), "save");
        expect(carol.id > 0, "id not populated after insert inside transaction");
        expect(co_await transaction.rollback(), "rollback() returned false");
      }
      // carolId was populated by the insert even though the row never actually landed —
      // this is the caveat: any in-memory copy from before a rollback is stale.
      auto found = unwrap(co_await User::find(carolId), "find after rollback");
      expect(not found.has_value(), "expected row to not exist after rollback, but find() succeeded");
    });

    // --- 11. Leave the table empty for the next run ---
    co_await runner.run("final cleanup",
                        []() -> Task<void> { unwrap(co_await User::bulkDestroy({true}), "bulkDestroy"); });

    json summary = runner.toJson();
    int status = summary["allPassed"].get<bool>() ? 200 : 500;
    co_return HttpResponse(status, "application/json", summary.dump(2));
  });

  // ---------------------------------------------------------------------------
  // GET /tests/db/relations_test
  // ---------------------------------------------------------------------------
  router.get("/tests/db/relations_test", [db](const HttpRequest &request) -> Task<Response> {
    using namespace models;
    using namespace testutil;
    using Pu = Predicate<User>;
    using Pp = Predicate<Post>;
    using Pfl = Predicate<PostLike<User, Post>>;
    using Pf = Predicate<Follow<User>>;

    TestRunner runner;

    // =========================================================================
    // 0. Cleanup – start from a known-empty state
    // =========================================================================
    co_await runner.run("cleanup", [db]() -> Task<void> {
      // Order matters because of FKs / cascades
      unwrap(co_await PostLike<User, Post>::bulkDestroy({true}), "PostLike bulkDestroy");
      unwrap(co_await Follow<User>::bulkDestroy({true}), "Follow bulkDestroy");
      unwrap(co_await Post::bulkDestroy({true}), "Post bulkDestroy");
      unwrap(co_await User::bulkDestroy({true}), "User bulkDestroy");

      // Also wipe the default through table used by friendship
      // (DefaultThroughModel creates "user_friend_user")
      // If you have a direct way to clear it, use it; otherwise the cascade
      // from User destroy should be enough in most setups.
      expect(unwrap(co_await User::all().count(), "count") == 0, "users not empty");
      expect(unwrap(co_await Post::all().count(), "count") == 0, "posts not empty");
    });

    // =========================================================================
    // 1. Seed users
    // =========================================================================
    User alice, bob, charlie, diana;
    co_await runner.run("seed users", [&]() -> Task<void> {
      alice = {.name = "Alice", .email = "alice@example.com", .age = 30};
      bob = {.name = "Bob", .email = "bob@example.com", .age = 28};
      charlie = {.name = "Charlie", .email = "charlie@example.com", .age = 25};
      diana = {.name = "Diana", .email = "diana@example.com", .age = 22};

      unwrap(co_await alice.save(), "alice save");
      unwrap(co_await bob.save(), "bob save");
      unwrap(co_await charlie.save(), "charlie save");
      unwrap(co_await diana.save(), "diana save");

      expect(alice.id > 0 && bob.id > 0 && charlie.id > 0 && diana.id > 0, "ids not set");
    });

    // =========================================================================
    // 2. Many-to-one  (Post → User)  + reverse one-to-many
    // =========================================================================
    Post post1, post2;
    co_await runner.run("many-to-one Post.user + reverse related()", [&]() -> Task<void> {
      post1 = {.title = "Hello", .content = "world", .user = alice.id};
      post2 = {.title = "Second", .content = "post", .user = alice.id};

      unwrap(co_await post1.save(), "post1 save");
      unwrap(co_await post2.save(), "post2 save");

      // Forward: ref<>()
      auto author = unwrap(co_await post1.ref<User>().first(), "ref author");
      expect(author.has_value() && author->id == alice.id, "ref() did not return Alice");

      // Reverse: related<>()
      auto alicePosts = unwrap(co_await alice.related<Post>().select(), "related posts");
      expect(alicePosts.size() == 2, "expected 2 posts for Alice, got " + std::to_string(alicePosts.size()));

      // Explicit related name (should be the same)
      auto alicePostsNamed = unwrap(co_await alice.related<Post, "user_posts">().select(), "named related");
      expect(alicePostsNamed.size() == 2, "named related mismatch");
    });

    // =========================================================================
    // 3. Optional FK + OnDelete::SET_NULL
    // =========================================================================
    co_await runner.run("OnDelete::SET_NULL", [&]() -> Task<void> {
      // Destroy Alice – posts should survive with user_id = NULL
      unwrap(co_await alice.destroy(), "destroy alice");

      auto p1 = unwrap(co_await Post::find(post1.id), "find post1");
      auto p2 = unwrap(co_await Post::find(post2.id), "find post2");
      expect(p1.has_value() && not p1->user.has_value(), "post1.user should be null after SET_NULL");
      expect(p2.has_value() && not p2->user.has_value(), "post2.user should be null after SET_NULL");

      // Re-create Alice so later tests have her
      alice = {.name = "Alice", .email = "alice@example.com", .age = 30};
      unwrap(co_await alice.save(), "recreate alice");
    });

    // =========================================================================
    // 4. Self-referential many-to-one  (bestFriend / mother)
    // =========================================================================
    co_await runner.run("self-ref many-to-one (bestFriend + mother)", [&]() -> Task<void> {
      bob.bestFriend = alice.id;
      bob.mother = diana.id;
      unwrap(co_await bob.save(), "bob update");

      // Forward
      auto best = unwrap(co_await bob.ref<User, &User::bestFriend>().first(), "ref bestFriend");
      expect(best.has_value() && best->id == alice.id, "bestFriend mismatch");

      auto mom = unwrap(co_await bob.ref<User, &User::mother>().first(), "ref mother");
      expect(mom.has_value() && mom->id == diana.id, "mother mismatch");

      // Reverse (relatedName)
      auto bestFriendsOfAlice = unwrap(co_await alice.related<User, "best_friends">().select(), "best_friends");
      expect(bestFriendsOfAlice.size() == 1 && bestFriendsOfAlice[0].id == bob.id,
             "expected Bob as best_friend of Alice");

      auto childrenOfDiana = unwrap(co_await diana.related<User, "children">().select(), "children");
      expect(childrenOfDiana.size() == 1 && childrenOfDiana[0].id == bob.id, "expected Bob as child of Diana");
    });

    // =========================================================================
    // 5. Many-to-many – likes (Post ↔ User via PostLike) asymmetric
    // =========================================================================
    co_await runner.run("M2M likes – upsert / manyRelated / remove", [&]() -> Task<void> {
      // Re-assign posts to Alice
      post1.user = alice.id;
      post2.user = alice.id;

      unwrap(co_await post1.save(), "re-assign post1");
      unwrap(co_await post2.save(), "re-assign post2");

      // ---------- A. Call from the DEFINER side first (Post) ----------
      auto cnt = unwrap(co_await post1.upsertRelation<User, "likes">(alice), "post1 likes alice");
      expect(cnt == 1, "expected 1 from Post side");

      // Idempotent
      cnt = unwrap(co_await post1.upsertRelation<User, "likes">(alice), "post1 likes alice again");
      expect(cnt == 0, "expected 0 on duplicate");

      // ---------- B. Call from the TARGET side (User) ----------
      cnt = unwrap(co_await bob.upsertRelation<Post, "likes">(post1), "bob likes post1");
      expect(cnt == 1, "bob like insert");

      // ---------- C. manyRelated from both sides ----------
      auto aliceLikes = unwrap(co_await alice.manyRelated<Post, "likes">().select(), "alice likes");
      expect(aliceLikes.size() == 1, "Alice should like 1 post so far");

      auto post1Likers = unwrap(co_await post1.manyRelated<User, "likes">().select(), "post1 likers");
      expect(post1Likers.size() == 2, "post1 should have 2 likers (alice + bob)");

      // ---------- D. remove ----------
      auto removed = unwrap(co_await alice.removeRelation<Post, "likes">(post1), "remove like");
      expect(removed == 1, "expected 1 row removed");

      aliceLikes = unwrap(co_await alice.manyRelated<Post, "likes">().select(), "after remove");
      expect(aliceLikes.empty(), "Alice should have 0 likes left");
    });

    // =========================================================================
    // 6. Many-to-many with extra fields on through model (PostLike.likedAt)
    // =========================================================================
    co_await runner.run("M2M with through-object upsert (extra columns)", [&]() -> Task<void> {
      PostLike<User, Post> likeRow;
      likeRow.userId = charlie.id;
      likeRow.postId = post1.id;
      // likedAt is auto-generated

      auto cnt = unwrap(co_await Post::upsertRelationThrough<User, "likes">(likeRow), "upsert through obj");
      expect(cnt >= 1, "through upsert failed");

      auto charlieLikes = unwrap(co_await charlie.manyRelated<Post, "likes">().select(), "charlie likes");
      expect(charlieLikes.size() == 1 && charlieLikes[0].id == post1.id, "charlie should like post1");
    });

    // =========================================================================
    // 7. Symmetric many-to-many  – friendship (DOUBLE_ROW)
    // =========================================================================
    co_await runner.run("symmetric M2M friendship (DOUBLE_ROW)", [&]() -> Task<void> {
      // Alice ↔ Bob
      auto cnt = unwrap(co_await alice.upsertRelation<User, "friendship">(bob), "friend alice-bob");
      expect(cnt == 2, "DOUBLE_ROW should insert 2 rows"); // one each direction

      // Idempotent
      cnt = unwrap(co_await alice.upsertRelation<User, "friendship">(bob), "friend again");
      expect(cnt == 0, "should be 0 on duplicate");

      // Alice ↔ Charlie
      cnt = unwrap(co_await alice.upsertRelation<User, "friendship">(charlie), "friend alice-charlie");
      expect(cnt == 2, "second friendship");

      // Query both directions
      auto aliceFriends = unwrap(co_await alice.manyRelated<User, "friendship">().select(), "alice friends");
      expect(aliceFriends.size() == 2, "Alice should have 2 friends");

      auto bobFriends = unwrap(co_await bob.manyRelated<User, "friendship">().select(), "bob friends");
      expect(bobFriends.size() == 1 && bobFriends[0].id == alice.id, "Bob should only see Alice");

      // Remove (should delete both rows)
      auto removed = unwrap(co_await alice.removeRelation<User, "friendship">(bob), "unfriend");
      expect(removed == 2, "DOUBLE_ROW remove should delete 2 rows");

      aliceFriends = unwrap(co_await alice.manyRelated<User, "friendship">().select(), "after unfriend");
      expect(aliceFriends.size() == 1, "only Charlie left");
    });

    // =========================================================================
    // 8. Asymmetric self-referential M2M – follows (via Follow)
    // =========================================================================
    co_await runner.run("asymmetric M2M follows", [&]() -> Task<void> {
      // Alice follows Bob
      auto cnt = unwrap(co_await alice.upsertRelation<User, "follows">(bob), "alice follows bob");
      expect(cnt == 1, "follow insert");

      // Bob follows Alice (different direction – allowed)
      cnt = unwrap(co_await bob.upsertRelation<User, "follows">(alice), "bob follows alice");
      expect(cnt == 1, "reverse follow");

      // Alice follows Charlie
      cnt = unwrap(co_await alice.upsertRelation<User, "follows">(charlie), "alice follows charlie");
      expect(cnt == 1, "second follow");

      // Query following (FORWARD)
      auto aliceFollowing =
          unwrap(co_await alice.manyRelated<User, "follows">(LookupDirection::FORWARD).select(), "alice following");
      expect(aliceFollowing.size() == 2, "Alice should follow 2 people");

      // Query followers (REVERSE)
      auto aliceFollowers =
          unwrap(co_await alice.manyRelated<User, "follows">(LookupDirection::REVERSE).select(), "alice followers");
      expect(aliceFollowers.size() == 1 && aliceFollowers[0].id == bob.id, "Alice should have 1 follower (Bob)");

      // Remove
      auto removed = unwrap(co_await alice.removeRelation<User, "follows">(bob), "unfollow");
      expect(removed == 1, "unfollow count");

      aliceFollowing = unwrap(co_await alice.manyRelated<User, "follows">().select(), "after unfollow");
      expect(aliceFollowing.size() == 1, "only Charlie left");
    });

    // =========================================================================
    // 9. Through model with extra data + unique constraint
    // =========================================================================
    co_await runner.run("Follow through model extra fields + uniqueness", [&]() -> Task<void> {
      Follow<User> f;
      f.follower = diana.id;
      f.followee = alice.id;
      f.randomNum = 42;
      f.str = "hello";

      auto cnt = unwrap(co_await User::upsertRelationThrough<User, "follows">(f), "upsert follow with extras");
      expect(cnt >= 1, "follow with extras failed");
    });

    // =========================================================================
    // 10. Cascade delete on through tables
    // =========================================================================
    co_await runner.run("CASCADE on through tables", [&]() -> Task<void> {
      // Destroy Charlie – his likes and follows should disappear
      unwrap(co_await charlie.destroy(), "destroy charlie");

      auto remainingLikes = unwrap(co_await PostLike<User, Post>::all().count(), "likes count");
      // Only the likes that did not involve Charlie should remain
      // (exact number depends on previous steps, but Charlie’s row must be gone)

      auto charlieFollows = unwrap(co_await Follow<User>::filter(Pf::equals(&Follow<User>::follower, charlie.id) or
                                                                 Pf::equals(&Follow<User>::followee, charlie.id))
                                       .count(),
                                   "charlie follows left");
      expect(charlieFollows == 0, "Charlie’s follow rows should have been cascaded");
    });

    // =========================================================================
    // 11. Empty / missing relation edge cases
    // =========================================================================
    co_await runner.run("empty relation queries", [&]() -> Task<void> {
      User lonely = {.name = "Lonely", .email = "lonely@example.com"};
      unwrap(co_await lonely.save(), "lonely save");

      auto friends = unwrap(co_await lonely.manyRelated<User, "friendship">().select(), "lonely friends");
      expect(friends.empty(), "lonely should have 0 friends");

      auto following = unwrap(co_await lonely.manyRelated<User, "follows">().select(), "lonely following");
      expect(following.empty(), "lonely should follow 0");

      auto posts = unwrap(co_await lonely.related<Post>().select(), "lonely posts");
      expect(posts.empty(), "lonely should have 0 posts");
    });

    // =========================================================================
    // Final cleanup (optional – keeps DB tidy)
    // =========================================================================
    co_await runner.run("final cleanup", []() -> Task<void> {
      unwrap(co_await PostLike<User, Post>::bulkDestroy({true}), "final PostLike");
      unwrap(co_await Follow<User>::bulkDestroy({true}), "final Follow");
      unwrap(co_await Post::bulkDestroy({true}), "final Post");
      unwrap(co_await User::bulkDestroy({true}), "final User");
    });

    json summary = runner.toJson();
    int status = summary["allPassed"].get<bool>() ? 200 : 500;
    co_return HttpResponse(status, "application/json", summary.dump(2));
  });
}
