#pragma once

#include <string>
#include <vector>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpTypes.hpp>
#include <rukh/core/Task.hpp>

namespace rukh {

struct CorsConfig {
  std::vector<std::string> allowedOrigins;
  std::vector<std::string> allowedHeaders = {"Authorization", "Content-Type"};
  bool allowCredentials = true;
  std::string maxAge = "86400";
};

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
} // namespace rukh
