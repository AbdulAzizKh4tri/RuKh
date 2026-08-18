#include <rukh/middleware/CacheControlMiddleware.hpp>

namespace rukh {

CacheControlMiddleware::CacheControlMiddleware() {}
CacheControlMiddleware::CacheControlMiddleware(CacheControlConfig config) : config_(config) {}

core::Task<Response> CacheControlMiddleware::operator()(const HttpRequest &request, Next next) {
  Response response = co_await next();

  std::visit(
      [this, &request](auto &res) {
        if (res.headers.getHeaderLower("cache-control") != "")
          return;

        if (res.getStatusCode() >= 400) {
          res.headers.setCacheControl("no-store");
          return;
        }

        const auto &reqPathParts = request.getPathParts();
        auto routeCache = std::find_if(config_.routeCacheControl.begin(), config_.routeCacheControl.end(),
                                       [&reqPathParts, &request](const auto &p) {
                                         std::string_view pattern = p.first;
                                         if (pattern[0] == '/')
                                           pattern.remove_prefix(1);
                                         auto currReqPart = reqPathParts.begin();

                                         while (!pattern.empty() && currReqPart != reqPathParts.end()) {
                                           auto end = pattern.find('/');
                                           std::string_view routePart = pattern.substr(0, end);
                                           if (routePart == "*")
                                             return std::next(currReqPart) == reqPathParts.end();
                                           if (routePart == "**")
                                             return true;
                                           if (*currReqPart != routePart)
                                             return false;
                                           currReqPart = std::next(currReqPart);
                                           if (end == std::string_view::npos)
                                             break;
                                           pattern.remove_prefix(end + 1);
                                         }

                                         if (currReqPart == reqPathParts.end())
                                           return (pattern == "/" or pattern == "") ? true : false;

                                         return false;
                                       });

        if (routeCache != config_.routeCacheControl.end()) {
          res.headers.setCacheControl(routeCache->second);
          return;
        }

        auto mimeType = res.headers.getHeaderLower("content-type");

        if (mimeType != "") {
          auto it = config_.mimeCacheControl.find(mimeType);
          if (it != config_.mimeCacheControl.end()) {
            res.headers.setCacheControl(it->second);
            return;
          }
        }

        res.headers.setCacheControl(config_.defaultCacheControl);
      },
      response);

  co_return response;
}

void CacheControlMiddleware::setRouteCacheControl(const std::string &routePattern,
                                                  const std::string &cacheControlHeader) {
  if (std::find_if(config_.routeCacheControl.begin(), config_.routeCacheControl.end(), [&routePattern](const auto &p) {
        return p.first == routePattern;
      }) != config_.routeCacheControl.end())
    throw std::runtime_error("Pattern already exists in route cache control");
  config_.routeCacheControl.emplace_back(routePattern, cacheControlHeader);
};

void CacheControlMiddleware::setMimeCacheControl(const std::string &mimeType, const std::string &cacheControlHeader) {
  config_.mimeCacheControl[mimeType] = cacheControlHeader;
};

void CacheControlMiddleware::setDefaultCacheControl(const std::string &cacheControlHeader) {
  config_.defaultCacheControl = cacheControlHeader;
}
} // namespace rukh
