#include "routes.hpp"

#include <nlohmann/json.hpp>

#include <rukh/core/Task.hpp>
#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/HttpResponse.hpp>

using json = nlohmann::json;
using namespace rukh;
using namespace rukh::http;

void registerRoutes(Router &router, const ErrorFactory &errorFactory, pool::ThreadPool *threadPool) {

  router.get("/random", [](const HttpRequest &request) -> core::Task<Response> {
    HttpResponse response(200);
    response.headers.setHeaderLower("content-type", "text/html");
    co_return response;
  });

  router.get("/", [](const HttpRequest &request) -> core::Task<Response> {
    auto name = request.getQueryParam("name");

    HttpResponse response(200);
    response.headers.setHeaderLower("content-type", "text/html");
    if (request.getMethod() == "HEAD")
      response.stripBody();
    co_return response;
  });

  router.post("/", [](HttpRequest &request) -> core::Task<Response> {
    json data = json::parse(co_await request.consumeBody());
    auto res = HttpResponse(200, "Hello, " + std::string(data["name"]) + "!");
    res.headers.setHeaderLower("content-type", "text/plain");
    co_return res;
  });

  router.put("/", [](HttpRequest &request) -> core::Task<Response> {
    co_return HttpResponse(200, co_await request.consumeBody());
  });
}
