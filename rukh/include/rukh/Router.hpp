#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <rukh/ErrorFactory.hpp>
#include <rukh/HttpRequest.hpp>
#include <rukh/HttpTypes.hpp>
#include <rukh/core/Task.hpp>

namespace rukh {

enum class RouterResponse { OK, NOT_FOUND, METHOD_NOT_ALLOWED };

struct StringHash {
  using is_transparent = void;
  size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
  size_t operator()(const std::string &s) const { return std::hash<std::string_view>{}(s); }
};

struct RouteNode {
  std::unordered_map<std::string, RouteNode, StringHash, std::equal_to<>> children;
  std::unique_ptr<RouteNode> paramChild;
  std::unique_ptr<RouteNode> wildcardChild;
  std::unique_ptr<RouteNode> deepWildcardChild;
  std::map<std::string, Handler> requestHandlers;
  std::vector<std::string> patternParts;
  std::string allowedMethods;
};

class Router {
public:
  Router(ErrorFactory &errorFactory);

  void get(std::string path, Handler handler);

  void post(std::string path, Handler handler);

  void put(std::string path, Handler handler);

  void patch(std::string path, Handler handler);

  void delete_(std::string path, Handler handler);

  void use(Middleware middleware);

  core::Task<Response> dispatch(HttpRequest &request);

  RouterResponse validate(const HttpRequest &request);

  std::string getAllowedMethodsString(const HttpRequest &request);

  std::string getAllowedMethodsString(RouteNode *pathNode);

private:
  RouteNode pathTreeRoot_;
  std::vector<Middleware> middlewares_;
  ErrorFactory &errorFactory_;
  std::unordered_set<std::string> registeredMethods_;

  core::Task<Response> runChain(HttpRequest &request, Handler &handler, size_t startIndex);

  RouteNode *findMatchingRouteEntry(const std::vector<std::string_view> &pathParts);

  RouteNode *backtrack(RouteNode *node, std::vector<std::string_view>::const_iterator first,
                       std::vector<std::string_view>::const_iterator last);

  std::vector<std::pair<std::string, std::string>> getPathParams(const std::vector<std::string> &patternParts,
                                                                 const std::vector<std::string_view> &pathParts);

  void addRoute(const std::string &routePattern, const std::string &method, Handler &handler);

  void validatePattern(const std::string &pattern, const std::vector<std::string> &parts);
};
} // namespace rukh
