#include <optional>
#include <rukh/http/middleware/StaticMiddleware.hpp>

#include <filesystem>

#include <rukh/ServerConfig.hpp>
#include <rukh/core/AsyncFileReader.hpp>
#include <rukh/core/AsyncFileWriter.hpp>
#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/HttpFileResponse.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpResponse.hpp>
#include <rukh/http/HttpStreamResponse.hpp>
#include <rukh/http/MimeTypes.hpp>
#include <rukh/http/compression/CompressibleMimeTypes.hpp>
#include <rukh/http/compression/CompressorFactory.hpp>
#include <rukh/http/httpUtils.hpp>

namespace rukh::http::middleware {

bool etagMatches(std::string_view cacheHeader, const std::string &etag) {
  while (not cacheHeader.empty()) {
    auto end = cacheHeader.find(',');
    std::string_view token = cacheHeader.substr(0, end);
    trim(token);
    if (token == etag)
      return true;
    if (end == std::string_view::npos)
      break;
    cacheHeader.remove_prefix(end + 1);
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

std::string generateETag(std::filesystem::file_time_type mtime, uintmax_t size, const std::string &compression = "") {
  return '"' + std::to_string(mtime.time_since_epoch().count()) + '-' + std::to_string(size) + '-' + compression + '"';
}

StaticMiddleware::StaticMiddleware(ErrorFactory &errorFactory, StaticConfig config)
    : config_(config), errorFactory_(errorFactory) {
  canonicalRoot_ = std::filesystem::weakly_canonical(config_.root);
  compressedRoot_ = std::filesystem::weakly_canonical(ServerConfig::STATIC_CACHE_DIR);
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

core::Task<std::optional<core::CachedFile>> StaticMiddleware::lookupOrOpen(const std::string &key, int &errorStatus) {
  auto alreadyExistingFile = core::tl_file_cache.get(key);
  if (alreadyExistingFile.has_value())
    co_return alreadyExistingFile;

  const auto err = [&errorStatus](int status) {
    errorStatus = status;
    return std::nullopt;
  };

  std::filesystem::path resolved = std::filesystem::weakly_canonical(canonicalRoot_ / key);
  if (not resolved.native().starts_with(canonicalRoot_.native()))
    co_return err(403);

  std::filesystem::directory_entry entry(resolved);
  if (not entry.exists())
    co_return err(404);

  if (entry.is_directory()) {
    resolved /= "index.html";
    entry.assign(resolved);
    if (not entry.exists())
      co_return err(404);
  }

  int fd = ::open(resolved.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd == -1)
    co_return err((errno == EACCES || errno == EPERM) ? 403 : 500);

  core::CachedFile cf;
  cf.fd = fd;
  cf.size = entry.file_size();
  cf.mtime = entry.last_write_time();
  cf.resolvedPath = resolved;
  cf.mime = getOrDefault(MIME_TYPES, resolved.extension().string(), "application/octet-stream");
  cf.etag = generateETag(cf.mtime, cf.size);

  if (cf.size < ServerConfig::FILE_CONTENT_CACHE_THRESHOLD) {
    core::AsyncFileReader reader(fd, cf.size, /*owns=*/false);
    cf.cachedContent = std::make_shared<std::vector<unsigned char>>();
    co_await reader.readAllInto(*cf.cachedContent);
  }

  core::tl_file_cache.insert(key, cf);

  co_return cf;
}

core::Task<std::optional<core::CachedFile>>
StaticMiddleware::lookupOrBuildCompressed(const std::string &relative, const std::string &encoding,
                                          const core::CachedFile &source, compression::ICompressor &compressor,
                                          int &errorStatus) {
  std::string key = relative + "|" + encoding;

  auto alreadyExistingFile = core::tl_file_cache.get(key);
  if (alreadyExistingFile.has_value())
    co_return alreadyExistingFile;

  // Miss: do the async compression work outside the lock, then insert synchronously.
  core::AsyncFileReader src(source.fd, source.size, /*owns=*/false);
  std::string raw = co_await src.readAll();
  std::string compressed = compressor.compress(raw);

  std::filesystem::path compressedPath = compressedRoot_ / (relative + "." + encoding);
  std::error_code dirEc;
  std::filesystem::create_directories(compressedPath.parent_path(), dirEc);

  static std::atomic<uint64_t> threadIdCounter = 0;
  thread_local uint64_t myThreadId = threadIdCounter++;
  thread_local uint64_t tmpCounter = 0;
  std::filesystem::path tmpPath =
      compressedPath.string() + ".tmp." + std::to_string(myThreadId) + "." + std::to_string(tmpCounter++);

  std::optional<core::AsyncFileWriter> writerOpt = core::AsyncFileWriter::open(tmpPath);
  if (not writerOpt) {
    errorStatus = 500;
    co_return std::nullopt;
  }
  if (not co_await writerOpt->writeAll(compressed)) {
    std::filesystem::remove(tmpPath);
    errorStatus = 500;
    co_return std::nullopt;
  }
  std::error_code renameEc;
  std::filesystem::rename(tmpPath, compressedPath, renameEc);
  if (renameEc) {
    std::filesystem::remove(tmpPath);
    errorStatus = 500;
    co_return std::nullopt;
  }

  int fd = ::open(compressedPath.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd == -1) {
    errorStatus = 500;
    co_return std::nullopt;
  }
  core::CachedFile cf;
  cf.fd = fd;
  cf.size = compressed.size();
  cf.mtime = source.mtime;
  cf.resolvedPath = compressedPath;
  cf.mime = source.mime;
  cf.etag = generateETag(source.mtime, cf.size, encoding);

  core::tl_file_cache.insert(key, cf);
  co_return cf;
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

  int errorStatus = 0;
  auto cachedOpt = co_await lookupOrOpen(relative, errorStatus);
  if (not cachedOpt) {
    if (errorStatus == 404)
      co_return co_await next();
    co_return buildErrorResponse(request, errorStatus);
  }

  const core::CachedFile &cachedFile = *cachedOpt;

  auto fileSize = cachedFile.size;
  std::filesystem::path resolved = cachedFile.resolvedPath;
  std::string mime = cachedFile.mime;

  bool hasRangeHeader = not request.getHeaderLower("range").empty();

  bool isCompressible = not hasRangeHeader;
  isCompressible = isCompressible and ServerConfig::ENABLE_STATIC_COMPRESSION and
                   not request.getHeaderLower("accept-encoding").empty();
  isCompressible =
      isCompressible && std::find(compression::compressibleMimeTypes.begin(), compression::compressibleMimeTypes.end(),
                                  mime) != compression::compressibleMimeTypes.end();
  isCompressible = isCompressible && fileSize >= ServerConfig::COMPRESS_MIN_BYTES;

  int contentFd = cachedFile.fd;
  uintmax_t contentSize = fileSize;
  std::string eTag = cachedFile.etag;
  std::string finalEncoding = "";
  Compressor compressor;

  if (isCompressible) {
    const auto acceptEncodingHeader = toLowerCase(request.getHeaderLower("accept-encoding"));
    std::vector<std::pair<std::string, float>> encodingPrefs;
    std::vector<std::string> excluded;

    parseQValues(acceptEncodingHeader, encodingPrefs, excluded);

    std::sort(encodingPrefs.begin(), encodingPrefs.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    bool identityAllowed = std::find(excluded.begin(), excluded.end(), "identity") == excluded.end();

    for (const auto &encoding : encodingPrefs) {
      compressor = compression::getCompressor(encoding.first);
      if (compressor || encoding.first == "identity")
        break;
    }

    if (not compressor && not identityAllowed)
      co_return buildErrorResponse(request, 406);

    if (compressor) {
      finalEncoding = compressor->getEncoding();
      int compErrorStatus = 0;
      auto compressedOpt =
          co_await lookupOrBuildCompressed(relative, finalEncoding, cachedFile, *compressor, compErrorStatus);
      if (not compressedOpt)
        co_return buildErrorResponse(request, compErrorStatus);

      contentFd = compressedOpt->fd;
      contentSize = compressedOpt->size;
      eTag = compressedOpt->etag;
    }
  }

  auto cacheControl = getOrDefault(config_.mimeCacheControl, mime, config_.defaultCacheControl);
  auto lastWrite = std::chrono::file_clock::to_sys(cachedFile.mtime);
  auto lastWriteSeconds = std::chrono::time_point_cast<std::chrono::seconds>(lastWrite);
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
  if (not validateAndCleanRanges(ranges, contentSize)) {
    co_return buildErrorResponse(request, 416);
  }

  bool useRange = false;
  if (not ranges.empty()) {
    if (ifRange.empty()) {
      useRange = true;
    } else {
      if (not ifRange.empty() && ifRange.front() == '"') {
        useRange = etagMatches(ifRange, eTag);
      } else {
        auto ifRangeDate = parseHttpDate(std::string(ifRange));
        if (ifRangeDate.has_value())
          useRange = (lastWriteSeconds <= *ifRangeDate);
      }
    }
  }

  size_t totalSize = 0;
  bool isMultiPart = ranges.size() > 1;

  std::string boundaryCore = "Boundary" + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()));
  std::string boundaryDelimiter = "--" + boundaryCore;
  std::string partContentType = "";

  if (useRange and isMultiPart) {
    partContentType = "content-type: " + mime + "\r\n";
    size_t contentRangeBaseSize =
        sizeof("content-range: bytes ") - 1 + sizeof("-") - 1 + sizeof("/") - 1 + sizeof("\r\n") - 1;
    for (const auto &[start, end] : ranges) {
      totalSize += boundaryDelimiter.size() + 2;
      size_t contentRangeSize =
          contentRangeBaseSize + digit_count(*start) + digit_count(*end) + digit_count(contentSize);
      totalSize += contentRangeSize;
      totalSize += partContentType.size();
      totalSize += 2;
      totalSize += *end - *start + 1 + 2;
    }
    totalSize += boundaryDelimiter.size() + 4;
  } else if (useRange) {
    totalSize = ranges[0].second.value() - ranges[0].first.value() + 1;
  } else {
    totalSize = contentSize;
  }

  if (method == "HEAD") {
    HttpResponse response;

    if (isMultiPart) {
      response.setStatusCode(206);
      response.headers.setHeaderLower("content-type", "multipart/byteranges; boundary=" + boundaryCore);
    } else if (useRange) {
      response.setStatusCode(206);
      response.headers.setHeaderLower("content-type", mime);
      response.headers.setContentRange(*ranges[0].first, *ranges[0].second, contentSize);
    } else {
      response.setStatusCode(200);
      response.headers.setHeaderLower("content-type", mime);
    }

    if (compressor)
      response.headers.setHeaderLower("content-encoding", finalEncoding);

    response.headers.setHeaderLower("content-length", std::to_string(totalSize));
    response.headers.addHeaderLower("accept-ranges", "bytes");
    response.headers.addHeaderLower("vary", "accept-encoding");

    addCacheHeaders(response, eTag, lastWrite, cacheControl);
    co_return response;
  }

  if (isMultiPart) {
    core::AsyncFileReader file(contentFd, contentSize, /*owns=*/false);

    HttpStreamResponse response(206);
    response.headers.setHeaderLower("accept-ranges", "bytes");
    response.headers.setHeaderLower("content-length", std::to_string(totalSize));
    response.setChunked(false);
    response.headers.setHeaderLower("content-type", "multipart/byteranges; boundary=" + boundaryCore);

    auto nextBlock = [file = std::move(file), ranges = std::move(ranges), boundaryDelimiter, boundaryCore,
                      fileSize = contentSize, start = static_cast<size_t>(1), end = static_cast<size_t>(0),
                      idx = static_cast<int>(-1), mime,
                      sentClosingBoundary = false]() mutable -> core::Task<std::optional<std::string>> {
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
    addCacheHeaders(response, eTag, lastWrite, cacheControl);
    co_return response;
  } else {
    HttpFileResponse response(200, mime, resolved);
    if (cachedFile.cachedContent != nullptr)
      response.setCachedFileContent(cachedFile.cachedContent);
    response.setFd(contentFd, /*owns=*/false);
    response.setContentLength(totalSize);

    response.headers.addHeaderLower("accept-ranges", "bytes");
    response.headers.addHeaderLower("vary", "accept-encoding");

    if (compressor)
      response.headers.setHeaderLower("content-encoding", finalEncoding);

    addCacheHeaders(response, eTag, lastWrite, cacheControl);

    if (useRange) {
      response.setStatusCode(206);
      const size_t start = *ranges[0].first;
      response.setOffset(start);
      response.headers.setContentRange(start, *ranges[0].second, contentSize);
    }

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
