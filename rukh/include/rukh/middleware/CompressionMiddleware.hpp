#pragma once

#include <rukh/ErrorFactory.hpp>
#include <rukh/HttpRequest.hpp>
#include <rukh/HttpTypes.hpp>
#include <rukh/core/Task.hpp>

namespace rukh {

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
