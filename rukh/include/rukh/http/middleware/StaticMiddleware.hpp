/**
 * @file StaticMiddleware.hpp
 * @brief Middleware for serving static files.
 */
#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include <rukh/core/Task.hpp>
#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpTypes.hpp>

namespace rukh::http::middleware {

/// Config for StaticMiddleware
struct StaticConfig {
  /// The directory in the file system to serve static files from
  std::string root;
  /// The path prefix to serve static files from
  std::string prefix;
  /// cache control for different mime types
  std::unordered_map<std::string, std::string> mimeCacheControl;
  /// the default cache control header
  std::string defaultCacheControl = "max-age=5, public";
};

/**
 * @brief Middleware for serving static files. Short circuits the request if the file is found.
 *
 * Only GET and HEAD requests that match the prefix path are served, the rest pass through.
 * If file is found, decides whether to compress the file. Creates a compressed version of the file depending on the
 * request's compression preference.
 *
 * Checks cache headers, and acts accordingly.
 * Handles Range queries and sends partial content if requested.
 * Depending on the size of the file, the file is either sent or streamed.
 *
 * @note Handles it's own caching and compression, so must come before those middlewares. Also short circuits, so best
 * to add as early in the chain as possible.
 */
class StaticMiddleware {
public:
  StaticMiddleware(ErrorFactory &errorFactory, StaticConfig);
  StaticMiddleware(ErrorFactory &errorFactory, const std::string &root, const std::string &prefix);

  core::Task<Response> operator()(const HttpRequest &request, Next next);

  HttpResponse buildErrorResponse(const HttpRequest &request, const int statusCode,
                                  const std::string &message = "") const;

  void setRoot(const std::string &root);
  void setPrefix(const std::string &prefix);
  void setErrorFactory(const ErrorFactory &errorFactory);
  void setMimeCacheControl(const std::string &mimeType, const std::string &cacheControlHeader);
  void setDefaultCacheControl(const std::string &cacheControlHeader);

private:
  StaticConfig config_;
  ErrorFactory &errorFactory_;
  std::filesystem::path canonicalRoot_;
  std::filesystem::path compressedRoot_;
};
} // namespace rukh::http::middleware
