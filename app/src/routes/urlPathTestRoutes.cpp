#include <nlohmann/json.hpp>

#include <rukh/core/Task.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpResponse.hpp>

#include "routes/testRoutes.hpp"

void registerUrlPathTestRoutes(rukh::http::Router &router, const rukh::http::ErrorFactory &errorFactory,
                               rukh::pool::ThreadPool *threadPool) {
  using namespace rukh;
  using namespace rukh::http;
  using namespace nlohmann;

  // GET /tests/users/<id>
  router.get("/tests/users/<id>", [](const HttpRequest &request) -> core::Task<Response> {
    json j = {{"userId", request.getPathParam("id")}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  router.get("/tests/users/me/posts/", [](const HttpRequest &request) -> core::Task<Response> {
    json j = {{"user", "me"}, {"posts", {"post1", "post2", "post3"}}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/users/<userId>/posts/<postId>
  router.get("/tests/users/<userId>/posts/<postId>", [](const HttpRequest &request) -> core::Task<Response> {
    json j = {{"userId", request.getPathParam("userId")}, {"postId", request.getPathParam("postId")}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // DELETE /tests/items/<id>
  router.delete_("/tests/items/<id>",
                 [](const HttpRequest &request) -> core::Task<Response> { co_return HttpResponse(200); });

  // GET /tests/wildcard/* -- single segment
  router.get("/tests/wildcard/*", [](const HttpRequest &request) -> core::Task<Response> {
    json j = {{"path", request.getPathParam("*")}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });

  // GET /tests/deepwildcard/** -- greedy
  router.get("/tests/deepwildcard/**", [](const HttpRequest &request) -> core::Task<Response> {
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
  router.get("/tests/decode/path/<name>", [](const HttpRequest &request) -> core::Task<Response> {
    json j = {{"name", request.getPathParam("name")}};
    auto res = HttpResponse(200, j.dump());
    res.headers.setHeaderLower("content-type", "application/json");
    co_return res;
  });
}
