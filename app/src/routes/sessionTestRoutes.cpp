#include <nlohmann/json.hpp>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>
#include <rukh/core/Task.hpp>

#include "routes/testRoutes.hpp"

void registerSessionTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                               rukh::ThreadPool *threadPool) {
  using namespace rukh;
  using namespace nlohmann;

  // -- Session routes ----------------------------------------------------------

  // GET /tests/session/set?key=foo&value=bar
  router.get("/tests/session/set", [](HttpRequest &request) -> core::Task<Response> {
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
  router.get("/tests/session/get", [](HttpRequest &request) -> core::Task<Response> {
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
  router.get("/tests/session/all", [](HttpRequest &request) -> core::Task<Response> {
    auto session = co_await request.getSession();
    json body = json::object();
    for (const auto &[k, v] : session->getAll()) {
      body[k] = v;
    }
    HttpResponse res(200, body.dump());
    co_return res;
  });

  // GET /tests/session/delete?key=foo
  router.get("/tests/session/delete", [](HttpRequest &request) -> core::Task<Response> {
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
  router.get("/tests/session/invalidate", [](HttpRequest &request) -> core::Task<Response> {
    auto session = co_await request.getSession();
    session->invalidate();

    HttpResponse res(200, json{{"invalidated", true}}.dump());
    co_return res;
  });
}
