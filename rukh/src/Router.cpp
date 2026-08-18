#include <rukh/Router.hpp>

#include <unordered_map>
#include <unordered_set>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>
#include <rukh/HttpStreamResponse.hpp>
#include <rukh/core/utils.hpp>

namespace rukh {

Router::Router(ErrorFactory &errorFactory) : errorFactory_(errorFactory) {}

void Router::get(std::string path, Handler handler) { addRoute(path, "GET", handler); }

void Router::post(std::string path, Handler handler) { addRoute(path, "POST", handler); }

void Router::put(std::string path, Handler handler) { addRoute(path, "PUT", handler); }

void Router::patch(std::string path, Handler handler) { addRoute(path, "PATCH", handler); }

void Router::delete_(std::string path, Handler handler) { addRoute(path, "DELETE", handler); }

void Router::use(Middleware middleware) { middlewares_.push_back(std::move(middleware)); }

core::Task<Response> Router::dispatch(HttpRequest &request) {

  auto method = request.getMethod();

  if (method == "TRACE" || method == "CONNECT") {
    co_return errorFactory_.build(request, 501);
  }

  const auto &requestPath = request.getPath();

  if (method == "OPTIONS" && requestPath == "*") {
    auto methods = registeredMethods_;
    methods.insert("OPTIONS");
    if (methods.contains("GET"))
      methods.insert("HEAD");
    HttpResponse response(204);
    response.headers.setHeaderLower("allow", getCommaSeparatedString({methods.begin(), methods.end()}));
    co_return response;
  }

  auto pathNode = findMatchingRouteEntry(request.getPathParts());
  if (pathNode)
    request.setAttribute("allowedMethods", pathNode->allowedMethods);

  Handler terminal = [this, &pathNode](HttpRequest &req) -> core::Task<Response> {
    if (pathNode == nullptr) {
      auto response = errorFactory_.build(req, 404);
      if (req.getMethod() == "HEAD")
        response.stripBody();
      co_return response;
    }

    auto &definedMethods = pathNode->requestHandlers;

    const auto &pathParams = getPathParams(pathNode->patternParts, req.getPathParts());
    req.setPathParams(pathParams);

    auto lookupMethod = req.getMethod() == "HEAD" ? "GET" : req.getMethod();
    auto methodIt = definedMethods.find(lookupMethod);

    if (methodIt != definedMethods.end()) {
      Response response = co_await methodIt->second(req);

      if (req.getMethod() == "HEAD") {
        if (std::holds_alternative<HttpResponse>(response)) {
          std::get<HttpResponse>(response).stripBody();
        } else {
          auto &stream = std::get<HttpStreamResponse>(response);
          HttpResponse res(stream.getStatusCode());
          res.headers.removeHeader("content-length");
          for (auto &[k, v] : stream.headers.getAllHeaders())
            res.headers.setHeader(k, v);
          res.stripBody();
          response = res;
        }
      }
      co_return response;
    }

    if (req.getMethod() == "OPTIONS") {
      HttpResponse response(204);
      response.headers.setHeaderLower("allow", pathNode->allowedMethods);
      co_return response;
    }

    HttpResponse response = errorFactory_.build(req, 405);
    if (req.getMethod() == "HEAD")
      response.stripBody();
    response.headers.setHeaderLower("allow", pathNode->allowedMethods);
    co_return response;
  };

  co_return co_await runChain(request, terminal, 0);
}

RouterResponse Router::validate(const HttpRequest &request) {
  auto lookupMethod = (request.getMethod() == "HEAD") ? "GET" : request.getMethod();

  auto pathNode = findMatchingRouteEntry(request.getPathParts());
  if (pathNode == nullptr)
    return RouterResponse::NOT_FOUND;

  if (!pathNode->requestHandlers.contains(lookupMethod))
    return RouterResponse::METHOD_NOT_ALLOWED;

  return RouterResponse::OK;
}

std::string Router::getAllowedMethodsString(const HttpRequest &request) {
  auto pathNode = findMatchingRouteEntry(request.getPathParts());
  return getAllowedMethodsString(pathNode);
}

std::string Router::getAllowedMethodsString(RouteNode *pathNode) {
  if (pathNode == nullptr)
    return "";
  return pathNode->allowedMethods;
}

core::Task<Response> Router::runChain(HttpRequest &request, Handler &handler, size_t startIndex) {
  if (startIndex >= middlewares_.size()) {
    co_return co_await handler(request);
  }

  auto next = [&] { return runChain(request, handler, startIndex + 1); };
  auto &middleware = middlewares_[startIndex];
  co_return co_await middleware(request, next);
}

RouteNode *Router::findMatchingRouteEntry(const std::vector<std::string_view> &pathParts) {
  RouteNode *result = backtrack(&pathTreeRoot_, pathParts.begin(), pathParts.end());
  return (result && not result->requestHandlers.empty()) ? result : nullptr;
}

RouteNode *Router::backtrack(RouteNode *node, std::vector<std::string_view>::const_iterator first,
                             std::vector<std::string_view>::const_iterator last) {
  if (node == nullptr)
    return nullptr;
  if (first == last)
    return node;

  auto part = *first;

  if (auto it = node->children.find(part); it != node->children.end()) {
    if (auto *result = backtrack(&it->second, std::next(first), last))
      return result;
  }
  if (node->paramChild) {
    if (auto *result = backtrack(node->paramChild.get(), std::next(first), last))
      return result;
  }
  if (node->wildcardChild) {
    if (auto *result = backtrack(node->wildcardChild.get(), std::next(first), last))
      return result;
  }
  if (node->deepWildcardChild) {
    return backtrack(node->deepWildcardChild.get(), last, last);
  }

  return nullptr;
}

std::vector<std::pair<std::string, std::string>> Router::getPathParams(const std::vector<std::string> &patternParts,
                                                                       const std::vector<std::string_view> &pathParts) {

  std::vector<std::pair<std::string, std::string>> pathParams;

  for (size_t i = 0; i < patternParts.size(); i++) {
    if (patternParts[i] == "**") {
      std::string captured;
      for (size_t j = i; j < pathParts.size(); j++) {
        captured += pathParts[j];
        captured += "/";
      }
      if (!captured.empty())
        captured.pop_back();
      pathParams.emplace_back("**", captured);
      return pathParams;
    }
    if (patternParts[i] == "*") {
      pathParams.emplace_back("*", pathParts[i]);
      continue;
    }
    if (patternParts[i][0] == '<' && patternParts[i].back() == '>') {
      auto paramKey = patternParts[i].substr(1, patternParts[i].size() - 2);
      pathParams.emplace_back(paramKey, percentDecode(pathParts[i]));
    }
  }

  return pathParams;
}

void Router::addRoute(const std::string &routePattern, const std::string &method, Handler &handler) {
  auto pattern = routePattern;
  normalizePath(pattern);
  auto patternParts = split(pattern, "/");
  validatePattern(pattern, patternParts);

  RouteNode *node = &pathTreeRoot_;

  for (const auto &part : patternParts) {
    if (part.starts_with('<') && part.ends_with('>')) {
      if (!node->paramChild) {
        node->paramChild = std::make_unique<RouteNode>();
      }
      node = node->paramChild.get();
    } else if (part == "**") {
      if (!node->deepWildcardChild) {
        node->deepWildcardChild = std::make_unique<RouteNode>();
      }
      node = node->deepWildcardChild.get();
    } else if (part == "*") {
      if (!node->wildcardChild) {
        node->wildcardChild = std::make_unique<RouteNode>();
      }
      node = node->wildcardChild.get();
    } else {
      node = &node->children[part];
    }
  }

  if (node->requestHandlers.contains(method))
    throw std::invalid_argument("Duplicate route: " + pattern);
  node->requestHandlers.emplace(method, std::move(handler));
  node->patternParts = std::move(patternParts);
  if (node->allowedMethods.empty()) {
    node->allowedMethods = method;
    node->allowedMethods += ", OPTIONS";
  } else {
    node->allowedMethods += ", " + method;
  }

  if (method == "GET")
    node->allowedMethods += ", HEAD";

  registeredMethods_.insert(method);
}

void Router::validatePattern(const std::string &pattern, const std::vector<std::string> &parts) {

  std::unordered_set<std::string> params;

  auto isValidParamChar = [](char c) { return std::isalnum(c) || c == '_'; };

  for (size_t i = 0; i < parts.size(); i++) {
    if (parts[i].empty())
      throw std::invalid_argument("Empty segment in pattern: " + pattern);

    bool isLast = (i == parts.size() - 1);
    if ((parts[i] == "*" || parts[i] == "**") && !isLast)
      throw std::invalid_argument("Wildcard '" + parts[i] + "' must be the last segment in pattern: " + pattern);

    if (parts[i][0] == '<' && parts[i].back() == '>') {
      std::string param = parts[i].substr(1, parts[i].size() - 2);

      if (param.empty())
        throw std::invalid_argument("Parameter name cannot be empty: " + pattern);

      if (!std::ranges::all_of(param, isValidParamChar))
        throw std::invalid_argument("Parameter name '" + param +
                                    "' contains invalid characters in pattern: " + pattern);

      if (params.contains(param))
        throw std::invalid_argument("Duplicate parameter '" + param + "' in pattern: " + pattern);
      params.insert(param);
    }
  }
}
} // namespace rukh
