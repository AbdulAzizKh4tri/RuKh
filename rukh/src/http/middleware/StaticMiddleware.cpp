#include <rukh/http/middleware/StaticMiddleware.hpp>

#include <filesystem>

#include <rukh/ServerConfig.hpp>
#include <rukh/core/AsyncFileReader.hpp>
#include <rukh/core/AsyncFileWriter.hpp>
#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpResponse.hpp>
#include <rukh/http/HttpStreamResponse.hpp>
#include <rukh/http/MimeTypes.hpp>
#include <rukh/http/compression/CompressibleMimeTypes.hpp>
#include <rukh/http/compression/CompressorFactory.hpp>
#include <rukh/http/httpUtils.hpp>

namespace rukh::http::middleware {

bool etagMatches(std::string_view ifNoneMatch, const std::string &etag) {
  while (!ifNoneMatch.empty()) {
    auto end = ifNoneMatch.find(',');
    std::string_view token = ifNoneMatch.substr(0, end);
    trim(token);
    if (token == etag)
      return true;
    if (end == std::string_view::npos)
      break;
    ifNoneMatch.remove_prefix(end + 1);
  }
  return false;
}

std::string getNormalizedPath(std::string path) {
  auto newEnd = std::unique(path.begin(), path.end(), [](char a, char b) { return a == '/' && b == '/'; });
  path.erase(newEnd, path.end());

  if (!path.empty() && path.front() == '/')
    path.erase(0, 1);

  if (!path.empty() && path.back() == '/')
    path.pop_back();

  return path;
}

using Compressor = std::unique_ptr<compression::ICompressor>;

std::string constructContentRange(size_t start, size_t end, size_t fileSize) {
  std::string contentRange = "content-range: bytes ";
  contentRange += std::to_string(start);
  contentRange += "-";
  contentRange += std::to_string(end);
  contentRange += "/";
  contentRange += std::to_string(fileSize);
  return contentRange;
}

StaticMiddleware::StaticMiddleware(ErrorFactory &errorFactory, StaticConfig config)
    : config_(config), errorFactory_(errorFactory) {
  canonicalRoot_ = std::filesystem::weakly_canonical(config_.root);
  compressedRoot_ = std::filesystem::weakly_canonical(ServerConfig::STATIC_CACHE_DIR);
}

StaticMiddleware::StaticMiddleware(ErrorFactory &errorFactory, const std::string &root, const std::string &prefix)
    : errorFactory_(errorFactory) {
  config_.root = root;
  config_.prefix = getNormalizedPath(prefix);
  canonicalRoot_ = std::filesystem::weakly_canonical(root);
  compressedRoot_ = std::filesystem::weakly_canonical(ServerConfig::STATIC_CACHE_DIR);
}

std::string generateETag(const std::filesystem::directory_entry &entry, const std::string &compression = "") {
  auto mtime = entry.last_write_time().time_since_epoch().count();
  auto size = entry.file_size();
  return '"' + std::to_string(mtime) + '-' + std::to_string(size) + '-' + compression + '"';
}

template <typename Res>
void addCacheHeaders(Res &response, const std::string &etag,
                     const std::chrono::sys_time<std::chrono::file_clock::duration> lastWrite,
                     const std::string &cacheControl) {
  response.headers.setCacheControl(cacheControl);
  if (cacheControl.contains("no-store"))
    return;

  response.headers.setHeaderLower("etag", etag);
  response.headers.setHeaderLower("last-modified", toHttpDate(lastWrite));
}

core::Task<Response> StaticMiddleware::operator()(const HttpRequest &request, Next next) {

  const std::string &method = request.getMethod();

  if (not(method == "GET" || method == "HEAD"))
    co_return co_await next();

  const std::string &path = request.getPath();

  if (not(path == config_.prefix || path.starts_with(config_.prefix + "/")))
    co_return co_await next();

  std::string relative = path.substr(config_.prefix.size());
  if (not relative.empty() && relative.front() == '/')
    relative.erase(0, 1);

  std::filesystem::path resolved = std::filesystem::weakly_canonical(canonicalRoot_ / relative);

  if (not resolved.native().starts_with(canonicalRoot_.native())) {
    co_return buildErrorResponse(request, 403);
  }

  std::filesystem::directory_entry entry(resolved);
  if (not entry.exists())
    co_return co_await next();
  if (entry.is_directory()) {
    resolved /= "index.html";
    relative = relative.empty() ? "index.html" : relative + "/index.html";
    entry.assign(resolved);
    if (not entry.exists())
      co_return co_await next();
  }

  auto fileSize = entry.file_size();

  std::string extension = resolved.extension().string(); // e.g. ".html"
  std::string mime = getOrDefault(MIME_TYPES, extension, "application/octet-stream");

  bool hasRangeHeader = not request.getHeaderLower("range").empty();

  bool isCompressible = not hasRangeHeader;
  isCompressible =
      isCompressible && std::find(compression::compressibleMimeTypes.begin(), compression::compressibleMimeTypes.end(),
                                  mime) != compression::compressibleMimeTypes.end();
  isCompressible = isCompressible && fileSize >= ServerConfig::COMPRESS_MIN_BYTES;

  std::string compressedExtension = "";
  std::string finalEncoding = "";
  Compressor compressor;
  if (isCompressible) {
    const auto acceptEncodingHeader = toLowerCase(request.getHeaderLower("accept-encoding"));
    std::vector<std::pair<std::string, float>> encodingPrefs;
    std::vector<std::string> excluded;

    parseQValues(acceptEncodingHeader, encodingPrefs, excluded);

    std::sort(encodingPrefs.begin(), encodingPrefs.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    bool identityAllowed = true;

    if (std::find(excluded.begin(), excluded.end(), "identity") != excluded.end()) {
      identityAllowed = false;
    }

    for (const auto &encoding : encodingPrefs) {
      compressor = compression::getCompressor(encoding.first);
      if (compressor || encoding.first == "identity") {
        break;
      }
    }

    if (not compressor && not identityAllowed) {
      co_return buildErrorResponse(request, 406);
    }

    if (compressor) {
      finalEncoding = compressor->getEncoding();
      compressedExtension = "." + finalEncoding;
      auto compressedRelative = relative + compressedExtension;
      std::filesystem::path compressedPath = compressedRoot_ / compressedRelative;

      std::filesystem::directory_entry compressedEntry(compressedPath);
      bool originalFileModified = false;
      if (compressedEntry.exists()) {
        originalFileModified = entry.last_write_time() > compressedEntry.last_write_time();
        if (not originalFileModified) {
          entry.assign(compressedEntry);
          resolved = compressedPath;
          fileSize = entry.file_size();
        }
      }
      if (not compressedEntry.exists() || originalFileModified) {
        std::optional<core::AsyncFileReader> srcOpt = core::AsyncFileReader::open(resolved, fileSize);
        if (not srcOpt)
          co_return buildErrorResponse(request, 500);

        std::string raw = co_await srcOpt->readAll();
        std::string compressed = compressor->compress(raw);

        std::filesystem::create_directories(compressedPath.parent_path());

        static std::atomic<uint64_t> threadIdCounter = 0;
        thread_local uint64_t myThreadId = threadIdCounter++;
        thread_local uint64_t tmpCounter = 0;
        std::filesystem::path tmpPath =
            compressedPath.string() + ".tmp." + std::to_string(myThreadId) + "." + std::to_string(tmpCounter++);

        std::optional<core::AsyncFileWriter> writerOpt = core::AsyncFileWriter::open(tmpPath);
        if (not writerOpt)
          co_return buildErrorResponse(request, 500);

        bool ok = co_await writerOpt->writeAll(compressed);
        if (not ok) {
          std::filesystem::remove(tmpPath);
          co_return buildErrorResponse(request, 500);
        }

        std::error_code ec;
        std::filesystem::rename(tmpPath, compressedPath, ec);
        if (ec) {
          std::filesystem::remove(tmpPath);
          co_return buildErrorResponse(request, 500);
        }

        entry.assign(compressedPath);
        resolved = compressedPath;
        fileSize = compressed.size();
      }
    }
  }

  std::optional<core::AsyncFileReader> fileOpt = core::AsyncFileReader::open(resolved, fileSize);
  if (not fileOpt.has_value())
    co_return buildErrorResponse(request, 403);
  core::AsyncFileReader &file = fileOpt.value();

  // Design decision: checking for 304 AFTER the open in case permissions change.
  auto cacheControl = getOrDefault(config_.mimeCacheControl, mime, config_.defaultCacheControl);
  auto lastWrite = std::chrono::file_clock::to_sys(entry.last_write_time());
  auto lastWriteSeconds = std::chrono::time_point_cast<std::chrono::seconds>(lastWrite);
  std::string eTag = generateETag(entry, finalEncoding);
  auto ifRange = request.getHeaderLower("if-range");
  auto ifNoneMatch = request.getHeaderLower("if-none-match");
  auto ifModifiedSince = request.getHeaderLower("if-modified-since");

  if (not cacheControl.contains("no-store") && not hasRangeHeader) {
    if (not ifNoneMatch.empty() && etagMatches(ifNoneMatch, eTag)) {
      HttpResponse response(304);
      addCacheHeaders(response, eTag, lastWrite, cacheControl);
      response.headers.addHeaderLower("vary", "accept-encoding");
      response.headers.addHeaderLower("accept-ranges", "bytes");
      co_return response;
    }

    if (not ifModifiedSince.empty()) {
      auto dateOpt = parseHttpDate(std::string(ifModifiedSince));
      if (not dateOpt.has_value())
        co_return buildErrorResponse(request, 400);

      if (lastWriteSeconds <= *dateOpt) {
        HttpResponse response(304);
        addCacheHeaders(response, eTag, lastWrite, cacheControl);
        response.headers.addHeaderLower("vary", "accept-encoding");
        response.headers.addHeaderLower("accept-ranges", "bytes");
        co_return response;
      }
    }
  }

  auto ranges = request.getRanges();
  if (not validateAndCleanRanges(ranges, fileSize)) {
    co_return buildErrorResponse(request, 416);
  }

  bool allowRange = false;
  if (not ranges.empty()) {
    if (ifRange.empty()) {
      allowRange = true;
    } else {
      bool ifRangeMatched = false;

      if (not ifRange.empty() && ifRange.front() == '"') {
        ifRangeMatched = etagMatches(ifRange, eTag);
      } else {
        auto ifRangeDate = parseHttpDate(std::string(ifRange));
        if (ifRangeDate.has_value())
          ifRangeMatched = (lastWriteSeconds <= *ifRangeDate);
      }

      allowRange = ifRangeMatched;
    }
  }

  if (allowRange) {
    bool isMultiPart = ranges.size() > 1;

    size_t totalSize = 0;
    std::string boundaryCore = "Boundary" + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()));
    std::string boundaryDelimiter = "--" + boundaryCore;
    std::string partContentType = "";

    if (isMultiPart) {
      partContentType = "content-type: " + mime + "\r\n";
      size_t contentRangeBaseSize =
          sizeof("content-range: bytes ") - 1 + sizeof("-") - 1 + sizeof("/") - 1 + sizeof("\r\n") - 1;
      for (const auto &[start, end] : ranges) {
        totalSize += boundaryDelimiter.size() + 2; // --boundary + CRLF

        size_t contentRangeSize =
            contentRangeBaseSize + digit_count(*start) + digit_count(*end) + digit_count(fileSize);
        totalSize += contentRangeSize;
        totalSize += partContentType.size();
        totalSize += 2; // blank line

        totalSize += *end - *start + 1 + 2; // body + CRLF
      }
      totalSize += boundaryDelimiter.size() + 4; // --boundary-- + CRLF
    } else {
      totalSize = ranges[0].second.value() - ranges[0].first.value() + 1;
    }

    if (method == "HEAD") {
      HttpResponse response(206);
      if (isMultiPart) {
        response.headers.setHeaderLower("content-type", "multipart/byteranges; boundary=" + boundaryCore);
      } else {
        response.headers.setHeaderLower("content-type", mime);
        response.headers.setContentRange(*ranges[0].first, *ranges[0].second, fileSize);
      }
      response.headers.setHeaderLower("content-length", std::to_string(totalSize));
      response.headers.addHeaderLower("accept-ranges", "bytes");
      addCacheHeaders(response, eTag, lastWrite, cacheControl);
      co_return response;
    }

    if (totalSize <= ServerConfig::STATIC_STREAM_THRESHOLD_BYTES) {
      std::string body;
      body.reserve(totalSize);
      if (isMultiPart) {
        for (const auto &[start, end] : ranges) {
          body += boundaryDelimiter;
          body += "\r\n";

          body += constructContentRange(*start, *end, fileSize);
          body += "\r\n";
          body += partContentType;
          body += "\r\n";

          file.seek(*start);
          auto fileChunkOpt = co_await file.readChunk(*end - *start + 1);
          if (fileChunkOpt.has_value())
            body += *fileChunkOpt;
          body += "\r\n";
        }
        body += boundaryDelimiter;
        body += "--\r\n";
      } else {
        file.seek(*ranges[0].first);
        auto fileChunkOpt = co_await file.readChunk(totalSize);
        if (fileChunkOpt.has_value())
          body += *fileChunkOpt;
      }
      HttpResponse response(206, std::move(body));
      response.headers.addHeaderLower("accept-ranges", "bytes");
      if (isMultiPart) {
        response.headers.setHeaderLower("content-type", "multipart/byteranges; boundary=" + boundaryCore);
      } else {
        response.headers.setHeaderLower("content-type", mime);
        response.headers.setContentRange(*ranges[0].first, *ranges[0].second, fileSize);
      }
      addCacheHeaders(response, eTag, lastWrite, cacheControl);
      co_return response;
    } else {
      HttpStreamResponse response(206);
      response.headers.setHeaderLower("accept-ranges", "bytes");
      response.headers.setHeaderLower("content-length", std::to_string(totalSize));
      response.setChunked(false);
      if (isMultiPart) {
        response.headers.setHeaderLower("content-type", "multipart/byteranges; boundary=" + boundaryCore);
        auto nextBlock = [file = std::move(file), ranges = std::move(ranges), boundaryDelimiter, boundaryCore, fileSize,
                          start = static_cast<size_t>(1), end = static_cast<size_t>(0), idx = static_cast<int>(-1),
                          mime, sentClosingBoundary = false]() mutable -> core::Task<std::optional<std::string>> {
          std::string contentType = "content-type: " + mime + "\r\n";
          std::string body;
          body.reserve(ServerConfig::STATIC_STREAM_CHUNK_SIZE);

          if (start >= end) {
            idx++;
            if (idx == static_cast<int>(ranges.size())) {
              if (sentClosingBoundary)
                co_return std::nullopt;
              sentClosingBoundary = true;
              co_return std::string("--") + boundaryCore + "--\r\n";
            }
            start = *ranges[idx].first;
            end = *ranges[idx].second;
            body += boundaryDelimiter;
            body += "\r\n";
            body += constructContentRange(start, end, fileSize);
            body += "\r\n";
            body += contentType;
            body += "\r\n";
          }

          file.seek(start);
          size_t maxPayload = ServerConfig::STATIC_STREAM_CHUNK_SIZE > body.size()
                                  ? (ServerConfig::STATIC_STREAM_CHUNK_SIZE - body.size())
                                  : 0;
          if (maxPayload == 0)
            co_return body;

          size_t blockEnd = std::min(end, start + maxPayload - 1);
          auto blockSize = blockEnd - start + 1;
          start = blockEnd + 1;

          auto chunk = co_await file.readChunk(blockSize);
          if (not chunk.has_value())
            co_return std::nullopt;

          body += *chunk;
          if (blockEnd == end)
            body += "\r\n";

          co_return body;
        };
        response.setNextChunkFn(std::move(nextBlock));
      } else {
        auto start = *ranges[0].first;
        auto end = *ranges[0].second;
        response.headers.setHeaderLower("content-type", mime);
        response.headers.setContentRange(start, end, fileSize);
        auto nextBlock = [file = std::move(file), start, end]() mutable -> core::Task<std::optional<std::string>> {
          if (start > end)
            co_return std::nullopt;

          file.seek(start);
          auto blockEnd = std::min(end, start + ServerConfig::STATIC_STREAM_CHUNK_SIZE - 1);
          auto blockSize = blockEnd - start + 1;
          start = blockEnd + 1;
          co_return co_await file.readChunk(blockSize);
        };
        response.setNextChunkFn(std::move(nextBlock));
      }
      addCacheHeaders(response, eTag, lastWrite, cacheControl);
      co_return response;
    }
  }

  if (fileSize <= ServerConfig::STATIC_STREAM_THRESHOLD_BYTES) {
    std::string body = co_await file.readAll();
    HttpResponse response(200, mime, std::move(body));
    response.headers.addHeaderLower("accept-ranges", "bytes");
    if (compressor)
      response.headers.setHeaderLower("content-encoding", finalEncoding);
    response.headers.addHeaderLower("vary", "accept-encoding");
    addCacheHeaders(response, eTag, lastWrite, cacheControl);
    if (method == "HEAD")
      response.stripBody();
    co_return response;
  } else {
    if (method == "HEAD") {
      HttpResponse response(200);
      response.headers.addHeaderLower("accept-ranges", "bytes");
      if (compressor)
        response.headers.setHeaderLower("content-encoding", finalEncoding);
      response.headers.addHeaderLower("vary", "accept-encoding");
      response.headers.setHeaderLower("content-type", mime);
      response.headers.setHeaderLower("content-length", std::to_string(fileSize));
      addCacheHeaders(response, eTag, lastWrite, cacheControl);
      co_return response;
    }

    HttpStreamResponse response(200, [file = std::move(file)]() mutable -> core::Task<std::optional<std::string>> {
      co_return co_await file.readChunk(ServerConfig::STATIC_STREAM_CHUNK_SIZE);
    });
    response.setChunked(false);
    response.headers.setHeaderLower("content-length", std::to_string(fileSize));
    response.headers.addHeaderLower("accept-ranges", "bytes");
    if (compressor)
      response.headers.setHeaderLower("content-encoding", finalEncoding);
    response.headers.addHeaderLower("vary", "accept-encoding");
    response.headers.setHeaderLower("content-type", mime);
    addCacheHeaders(response, eTag, lastWrite, cacheControl);
    co_return response;
  }
}

void StaticMiddleware::setRoot(const std::string &root) {
  config_.root = root;
  canonicalRoot_ = std::filesystem::weakly_canonical(root);
}

void StaticMiddleware::setPrefix(const std::string &prefix) { config_.prefix = prefix; }

void StaticMiddleware::setErrorFactory(const ErrorFactory &errorFactory) { errorFactory_ = errorFactory; }

void StaticMiddleware::setMimeCacheControl(const std::string &mimeType, const std::string &cacheControlHeader) {
  config_.mimeCacheControl[mimeType] = cacheControlHeader;
};

void StaticMiddleware::setDefaultCacheControl(const std::string &cacheControlHeader) {
  config_.defaultCacheControl = cacheControlHeader;
}

HttpResponse StaticMiddleware::buildErrorResponse(const HttpRequest &request, const int statusCode,
                                                  const std::string &message) const {
  HttpResponse response = errorFactory_.build(request, statusCode, message);
  response.headers.addHeaderLower("accept-ranges", "bytes");
  if (request.getMethod() == "HEAD")
    response.stripBody();
  return response;
}
} // namespace rukh::http::middleware
