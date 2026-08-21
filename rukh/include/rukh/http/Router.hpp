/**
 * @file Router.hpp
 * @brief HTTP Router
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <rukh/core/Task.hpp>
#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpTypes.hpp>

namespace rukh::http {

enum class RouterResponse { OK, NOT_FOUND, METHOD_NOT_ALLOWED };

/**
 * @brief Node of the Route 'Trie'
 * @see Router
 */
struct RouteNode {
  std::unordered_map<std::string, RouteNode, StringHash, std::equal_to<>> children;

  /// parameter `/\<param\>/`
  std::unique_ptr<RouteNode> paramChild;

  /// wildcard /*
  std::unique_ptr<RouteNode> wildcardChild;

  /// deep wildcard /**
  std::unique_ptr<RouteNode> deepWildcardChild;

  /**
   * @brief Handlers for this route node
   * @tparam string Http Method
   */
  std::map<std::string, Handler> requestHandlers;

  /// Route pattern split by /
  std::vector<std::string> patternParts;

  std::string allowedMethods;
};

/**
 * @brief HTTP Router
 *
 * To define routes, use the `get`, `post`, `put`,  `patch` and  `delete_` methods.
 *
 * Example code:
 * @code
 * router.get("/", [](const HttpRequest &request) -> core::Task<Response> {
 *   co_return HttpResponse(200);
 * });
 * @endcode
 *
 * You can specify path parameters via `/\<param\>/` in the route pattern.
 * Wildcards can also be used, `*` for single segments and `**` for multiple segments.
 *
 * To use Middlewares, use the `Router::use method`.
 *
 * Example code:
 * @code
 * CorsMiddleware corsMiddleware;
 * corsMiddleware.setCorsOrigins({"http://localhost:8080", "https://localhost:8443"});
 * corsMiddleware.setCorsMaxAge(10);
 * router.use(corsMiddleware);
 * @endcode
 *
 * I recommend creating seperate functions for routes grouping, and passing the Router object to those functions.
 *
 * @see RouteNode
 * @see HttpTypes.hpp
 * @see HttpRequest
 * @see HttpResponse
 * @see HttpStreamResponse
 * @see Middleware
 */
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
} // namespace rukh::http
