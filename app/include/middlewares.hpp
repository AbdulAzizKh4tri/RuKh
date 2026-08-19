#pragma once

#include <memory>

#include <rukh/http/Router.hpp>
#include <rukh/http/middleware/CacheControlMiddleware.hpp>
#include <rukh/http/middleware/CompressionMiddleware.hpp>
#include <rukh/http/middleware/CorsMiddleware.hpp>
#include <rukh/http/middleware/SessionMiddleware.hpp>
#include <rukh/http/middleware/StaticMiddleware.hpp>
#include <rukh/http/session/InMemorySessionStore.hpp>

#include "errors.hpp"

inline void registerMiddlewares(rukh::http::Router &router, const std::string &host) {
  using namespace rukh;
  using namespace rukh::http;
  using namespace rukh::http::middleware;
  CorsMiddleware corsMiddleware;
  corsMiddleware.setCorsOrigins({"http://localhost:8080", "https://localhost:8443", "http://127.0.0.1:8080",
                                 "http://" + host + ":8080", "https://" + host + ":8443"});
  corsMiddleware.setCorsMaxAge(10);

  auto static_dir = std::filesystem::path(__FILE__).parent_path() / "../public";
  StaticMiddleware staticMiddleware(getErrorFactory(), static_dir, "static");
  staticMiddleware.setMimeCacheControl("text/css", "no-cache, no-store"); // just for testing

  SessionConfig sessionConfig;
  auto ttl = std::chrono::seconds(std::stoi(std::to_string(120)));
  auto sessionStore = std::make_unique<InMemorySessionStore>(ttl);
  SessionMiddleware sessionMiddleware(sessionConfig, std::move(sessionStore));

  CacheControlMiddleware cacheControlMiddleware;
  cacheControlMiddleware.setRouteCacheControl("/tests/debug/*", "max-age=5, public");
  cacheControlMiddleware.setMimeCacheControl("text/html", "max-age=5, public");
  cacheControlMiddleware.setDefaultCacheControl("no-cache, no-store");

  CompressionMiddleware compressionMiddleware(getErrorFactory());

  // First because all routes potentially need CORS, it doesn't modify the body so it's fine to put here
  router.use(corsMiddleware);
  // Must come before others because it has it's own caching/compression, short circuits chain if it can serve the file
  router.use(staticMiddleware);
  // Order doesn't matter after this
  router.use(compressionMiddleware);
  router.use(cacheControlMiddleware);
  router.use(std::move(sessionMiddleware));
}
