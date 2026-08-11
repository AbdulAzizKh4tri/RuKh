#include <nlohmann/json.hpp>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>

#include "routes/testRoutes.hpp"

void registerCookieTestRoutes(rukh::Router &router, const rukh::ErrorFactory &errorFactory,
                              rukh::ThreadPool *threadPool, rukh::db::IDatabase *db) {
  using namespace rukh;
  using namespace nlohmann;

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
}
