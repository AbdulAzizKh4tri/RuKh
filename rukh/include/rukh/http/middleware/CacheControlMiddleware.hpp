/**
 * @file CacheControlMiddleware.hpp
 * @brief Middleware for cache control
 */
#pragma once

#include <string>
#include <unordered_map>

#include <rukh/core/Task.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpTypes.hpp>

namespace rukh::http::middleware {
/// config for CacheControlMiddleware
struct CacheControlConfig {
  std::vector<std::pair<std::string, std::string>> routeCacheControl;
  std::unordered_map<std::string, std::string> mimeCacheControl;
  std::string defaultCacheControl = "no-store";
};

/**
 * @brief Middleware for cache control
 */
class CacheControlMiddleware {
public:
  CacheControlMiddleware();
  CacheControlMiddleware(CacheControlConfig config);

  core::Task<Response> operator()(const HttpRequest &request, Next next);

  void setMimeCacheControl(const std::string &mimeType, const std::string &cacheControlHeader);
  void setRouteCacheControl(const std::string &routePattern, const std::string &cacheControlHeader);
  void setDefaultCacheControl(const std::string &cacheControlHeader);

private:
  CacheControlConfig config_;
};
} // namespace rukh::http::middleware
