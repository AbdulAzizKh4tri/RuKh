#include <nlohmann/json.hpp>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>

#include "routes/testRoutes.hpp"

void registerBasicTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory, rukh::ThreadPool *threadPool,
                             rukh::db::IDatabase *db) {
  using namespace rukh;
  using namespace nlohmann;

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
}
