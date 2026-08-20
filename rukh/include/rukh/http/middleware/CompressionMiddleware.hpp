/**
 * @file CompressionMiddleware.hpp
 * @brief Compression middleware
 */
#pragma once

#include <rukh/core/Task.hpp>
#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpTypes.hpp>

namespace rukh::http::middleware {

/**
 * @brief Compression middleware
 *
 * If compression is needed on a request, does content negotiation and returns the correct encoded/compressed data.
 * If can't agree on encoding, returns a 406 Not Acceptable error to the user.
 *
 */
class CompressionMiddleware {
public:
  CompressionMiddleware(ErrorFactory &errorFactory);
  core::Task<Response> operator()(const HttpRequest &request, Next next);

private:
  ErrorFactory &errorFactory_;

  HttpResponse buildErrorResponse(const HttpRequest &request, const int statusCode,
                                  const std::string &message = "") const;
};
} // namespace rukh::http::middleware
