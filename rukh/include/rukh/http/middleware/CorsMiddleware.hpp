/**
 * @file CorsMiddleware.hpp
 * @brief Middleware for Cross Origin Resource Sharing.
 */

#pragma once

#include <string>
#include <vector>

#include <rukh/core/Task.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpTypes.hpp>

namespace rukh::http::middleware {

/// Cors config
struct CorsConfig {
  std::vector<std::string> allowedOrigins;
  std::vector<std::string> allowedHeaders = {"Authorization", "Content-Type"};
  bool allowCredentials = true;
  std::string maxAge = "86400";
};

/**
 * @brief Middleware for Cross Origin Resource Sharing.
 *
 * Short-circuits the request on OPTIONS requests, and sets CORS headers on non-OPTIONS requests.
 */
class CorsMiddleware {
public:
  CorsMiddleware();
  CorsMiddleware(CorsConfig corsConfig);

  core::Task<Response> operator()(const HttpRequest &request, Next next);

  void setCorsOrigins(const std::vector<std::string> &origins);

  void setCorsHeaders(const std::vector<std::string> &headers);

  void setCorsMaxAge(int maxAge);

  void setAccessControlAllowCredentials(bool allowCredentials);

private:
  CorsConfig corsConfig_;
  std::string allowedHeaders_;

  bool isOriginAllowed(const std::string_view origin);
};
} // namespace rukh::http::middleware
