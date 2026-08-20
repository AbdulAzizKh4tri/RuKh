#include <rukh/http/middleware/CorsMiddleware.hpp>

#include <rukh/TypeHelpers.hpp>
#include <rukh/http/HttpResponse.hpp>

namespace rukh::http::middleware {

CorsMiddleware::CorsMiddleware() {}
CorsMiddleware::CorsMiddleware(CorsConfig corsConfig) : corsConfig_(corsConfig) {
  allowedHeaders_ = getCommaSeparatedString(corsConfig_.allowedHeaders);
}

core::Task<Response> CorsMiddleware::operator()(const HttpRequest &request, Next next) {
  auto originView = request.getHeaderLower("origin");

  if (originView.empty())
    co_return co_await next(); // not a CORS request

  auto origin = originView;

  if (request.getMethod() == "OPTIONS") {
    HttpResponse response(204);
    std::string allowedMethods = request.getAttribute("allowedMethods");

    if (!isOriginAllowed(origin))
      co_return response;

    response.headers.addHeaderLower("vary", "origin");
    response.headers.setHeaderLower("access-control-allow-origin", origin);
    response.headers.setHeaderLower("access-control-allow-methods", allowedMethods);
    response.headers.setHeaderLower("access-control-allow-headers", allowedHeaders_);
    response.headers.setHeaderLower("access-control-max-age", corsConfig_.maxAge);
    if (corsConfig_.allowCredentials)
      response.headers.setHeaderLower("access-control-allow-credentials", "true");
    co_return response;
  }

  Response response = co_await next();

  std::visit(overloads{[&origin, this](auto &res) {
               if (not origin.empty()) {
                 if (isOriginAllowed(origin))
                   res.headers.setHeaderLower("access-control-allow-origin", origin);
                 res.headers.addHeaderLower("vary", "origin");
               }
             }},
             response);

  co_return response;
}

void CorsMiddleware::setCorsOrigins(const std::vector<std::string> &origins) { corsConfig_.allowedOrigins = origins; }

void CorsMiddleware::setCorsHeaders(const std::vector<std::string> &headers) {
  corsConfig_.allowedHeaders = headers;
  allowedHeaders_ = getCommaSeparatedString(corsConfig_.allowedHeaders);
}

void CorsMiddleware::setCorsMaxAge(int maxAge) { corsConfig_.maxAge = std::to_string(maxAge); }

void CorsMiddleware::setAccessControlAllowCredentials(bool allowCredentials) {
  corsConfig_.allowCredentials = allowCredentials;
}

bool CorsMiddleware::isOriginAllowed(const std::string_view origin) {
  for (const auto &allowedOrigin : corsConfig_.allowedOrigins) {
    if (allowedOrigin == origin || allowedOrigin == "*") {
      return true;
    }
  }
  return false;
}
} // namespace rukh::http::middleware
