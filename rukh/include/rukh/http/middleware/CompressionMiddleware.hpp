#pragma once

#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpTypes.hpp>
#include <rukh/core/Task.hpp>

namespace rukh::http::middleware {

class CompressionMiddleware {
public:
  CompressionMiddleware(ErrorFactory &errorFactory);
  core::Task<Response> operator()(const HttpRequest &request, Next next);

private:
  ErrorFactory &errorFactory_;

  HttpResponse buildErrorResponse(const HttpRequest &request, const int statusCode,
                                  const std::string &message = "") const;
};
} // namespace rukh
