#include <rukh/http/middleware/CompressionMiddleware.hpp>

#include <memory>
#include <string_view>
#include <variant>

#include <rukh/http/HttpResponse.hpp>
#include <rukh/http/compression/CompressibleMimeTypes.hpp>
#include <rukh/http/compression/CompressorFactory.hpp>
#include <rukh/http/compression/ICompressor.hpp>
#include <rukh/http/httpUtils.hpp>

namespace rukh::http::middleware {

using Compressor = std::unique_ptr<compression::ICompressor>;

CompressionMiddleware::CompressionMiddleware(ErrorFactory &errorFactory) : errorFactory_(errorFactory) {}

core::Task<Response> CompressionMiddleware::operator()(const HttpRequest &request, Next next) {
  const auto acceptEncodingHeaderRaw = request.getHeaderLower("accept-encoding");

  if (acceptEncodingHeaderRaw.empty())
    co_return co_await next();

  Response response = co_await next();

  bool mustSkip = false;
  std::visit(
      [&mustSkip](auto &res) {
        mustSkip = mustSkip || not res.headers.getHeaderLower("content-encoding").empty();
        mustSkip =
            mustSkip || std::find(compression::compressibleMimeTypes.begin(), compression::compressibleMimeTypes.end(),
                                  res.getContentType()) == compression::compressibleMimeTypes.end();
      },
      response);

  if (mustSkip)
    co_return response;

  std::vector<std::pair<std::string, float>> encodingPrefs;
  std::vector<std::string> excluded;
  auto acceptEncodingHeader = toLowerCase(acceptEncodingHeaderRaw);

  parseQValues(acceptEncodingHeader, encodingPrefs, excluded);

  bool identityAllowed = true;
  if (std::find(excluded.begin(), excluded.end(), "identity") != excluded.end()) {
    identityAllowed = false;
  }
  if (HttpResponse *res = std::get_if<HttpResponse>(&response)) {
    if (identityAllowed && res->getBodySize() < ServerConfig::COMPRESS_MIN_BYTES) {
      res->headers.addHeaderLower("vary", "accept-encoding");
      co_return response;
    }
  }

  std::sort(encodingPrefs.begin(), encodingPrefs.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });

  Compressor compressor;

  for (const auto &encoding : encodingPrefs) {
    compressor = compression::getCompressor(encoding.first);
    if (compressor) {
      break;
    }
  }

  if (not compressor) {
    if (identityAllowed)
      co_return response;
    co_return buildErrorResponse(request, 406);
  }

  if (HttpResponse *res = std::get_if<HttpResponse>(&response)) {
    res->setBody(compressor->compress(res->getBody()));
    res->headers.setHeaderLower("content-encoding", std::string(compressor->getEncoding()));
    res->headers.addHeaderLower("vary", "accept-encoding");

  } else if (HttpStreamResponse *resStream = std::get_if<HttpStreamResponse>(&response)) {

    resStream->headers.setHeaderLower("content-encoding", std::string(compressor->getEncoding()));
    resStream->headers.addHeaderLower("vary", "accept-encoding");

    resStream->setNextChunkFn([comp = std::move(compressor), originalFn = std::move(resStream->takeNextChunkFn()),
                               finished = false]() mutable -> core::Task<std::optional<std::string>> {
      if (finished)
        co_return std::nullopt;
      for (;;) {
        auto chunk = co_await originalFn();
        if (!chunk) {
          finished = true;
          auto tail = comp->finish();
          co_return tail.empty() ? std::nullopt : std::make_optional(tail);
        }
        auto compressed = comp->feedChunk(*chunk);
        if (!compressed.empty())
          co_return compressed;
      }
    });
  }

  co_return response;
}

HttpResponse CompressionMiddleware::buildErrorResponse(const HttpRequest &request, const int statusCode,
                                                       const std::string &message) const {
  HttpResponse response = errorFactory_.build(request, statusCode, message);
  if (request.getMethod() == "HEAD")
    response.stripBody();
  return response;
}
} // namespace rukh::http::middleware
