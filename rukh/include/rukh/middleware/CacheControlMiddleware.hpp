#pragma once

#include <string>
#include <unordered_map>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpTypes.hpp>
#include <rukh/core/Task.hpp>

namespace rukh {
struct CacheControlConfig {
  std::vector<std::pair<std::string, std::string>> routeCacheControl;
  std::unordered_map<std::string, std::string> mimeCacheControl;
  std::string defaultCacheControl = "no-store";
};

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
} // namespace rukh
