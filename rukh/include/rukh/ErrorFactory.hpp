/**
 * @file ErrorFactory.hpp
 * @brief Error Factory for easier error handling
 */
#pragma once

#include <functional>
#include <vector>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>

namespace rukh {

/// Returns a Formatted error Response
using Formatter = std::function<HttpResponse(int statusCode, const std::string_view &message)>;

class ErrorFactory {
public:
  ErrorFactory();

  /**
   * @brief Sets the fallback formatter. i.e the formmater that will be used if no formatter matches the
   * Accept-Encoding. has a Json formatter by default.
   *
   * @param type The content-type of the formatter Response
   * @param formatter The formatter function
   */
  void setFallbackFormatter(std::string type, Formatter formatter);

  std::pair<std::string, Formatter> getFallbackFormatter();

  /// Set a formatter for the given content type.
  void setFormatter(std::string type, Formatter formatter);

  /// Does content negotiation and returns an appropriatley formatted error response. Fallsback to the fallbackFormatter
  HttpResponse build(const HttpRequest &req, int statusCode, const std::string_view &message = "") const;

private:
  std::vector<std::pair<std::string, Formatter>> registeredFormatters_;
  std::pair<std::string, Formatter> fallbackFormatterPair_;
};
} // namespace rukh
