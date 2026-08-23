/**
 * @file ServerConfig.hpp
 * @brief Server configuration
 */
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace rukh {

/// Server Configuration
class ServerConfig {
public:
  static constexpr size_t KB = 1024;
  static constexpr size_t MB = 1024 * KB;
  static constexpr size_t GB = 1024 * MB;

  /// Set the server name sent with the `Server` HTTP response header.
  static void setServerName(const std::string &name) {
    if (name.empty())
      throw std::runtime_error("Server name cannot be empty");
    serverName_ = name;
    serverLine_ = "server: " + name + "\r\n";
  }

  static const std::string &getServerName() { return serverName_; }

  ///@brief Internal Method
  ///@internalMethod
  static const std::string &getServerLine() { return serverLine_; }

  /// @name Timeouts (seconds)

  /// Socker read/write inactivity timeout
  inline static int INACTIVITY_TIMEOUT_S = 20;
  /// Request formation timeout
  inline static int FORMATION_TIMEOUT_S = 120;
  /// Time to wait for graceful shutdown, before terminating (requests may still be in flight)
  inline static int GRACEFUL_SHUTDOWN_TIMEOUT_S = 20;

  /// Timeout for EpollInstance::wait
  inline static int EPOLL_WAIT_TIMEOUT = 1;

  /// @}

  /// @name Request/Response limits

  /// Bytes to read in a single ConnectionIO::read call. \todo Find a good value.
  inline static size_t SINGLE_READ_BYTES = 1 * KB;
  /// Maximum allowed size of request header
  inline static size_t MAX_HEADER_BYTES = 8 * KB;
  /// Maximum allowed size of request body in bytes
  inline static size_t MAX_CONTENT_LENGTH = 1 * MB;
  /// Transfer Encoding Chunked, maximum size allowed for a chunk
  inline static size_t MAX_TE_CHUNK_LENGTH = 1 * MB;
  /// Transfer Encoding Chunked, maximum size allowed for a request
  inline static size_t MAX_TE_LENGTH = 10 * MB;

  /// For Responses: How filled up the write buffer is allowed to get before we decide the connection is dead.
  inline static size_t MAX_WRITE_BUFFER_BYTES = 10 * MB;

  /// @}

  /// @name Multipart

  /**
   * @brief  Maximum permitted size of a multipart request when the request specifies its body size using the
   * Content-Length header.
   *
   * Requests exceeding this limit are rejected with HTTP 413 before multipart parsing begins.
   */
  inline static size_t MAX_MULTIPART_CONTENT_LENGTH = 50 * MB;

  /**
   * @breif Maximum permitted size of a multipart request received using Transfer-Encoding: chunked.
   * Since a chunked request does not provide a Content-Length, this value is used as the parser's upper bound for total
   * request consumption
   */
  inline static size_t MAX_MULTIPART_TE_LENGTH = 50 * MB;

  /// Maximum permitted size of an individual non-file multipart form field. This prevents a single form field from
  /// consuming an excessive amount of memory or request-processing resources.
  inline static size_t MAX_MULTIPART_FIELD_SIZE = 64 * KB;

  /// Maximum permitted size of an individual multipart file part. Each uploaded file is independently checked against
  /// this limit and exceeding it results in HTTP 413.
  inline static size_t MAX_MULTIPART_FILE_SIZE = 20 * MB;

  /// Maximum permitted size of an individual multipart part header
  inline static size_t MAX_MULTIPART_PART_HEADER_SIZE = 8 * KB;

  /// The maximum number of multipart parts allowed in a single request
  inline static size_t MAX_MULTIPART_PARTS = 256;

  /// Maximum permitted length of the multipart boundary extracted from the Content-Type header. Boundaries exceeding
  /// this limit are rejected as malformed requests with HTTP 400.
  inline static size_t MAX_MULTIPART_BOUNDARY_LENGTH = 64;

  /**
   * @brief Size of the internal buffer used by MultipartParser to incrementally read and process multipart request
   * bodies.
   *
   * A larger value can reduce the number of body-stream reads, while increasing per-request memory usage.
   */
  inline static size_t MULTIPART_BUFFER_SIZE = 4 * KB;

  /// @}

  /// @name Static File Serving

  /// The threshold at which a file is streamed instead of sent.
  inline static size_t STATIC_STREAM_THRESHOLD_BYTES = 5 * MB;

  /// The chunk size used when streaming files
  inline static size_t STATIC_STREAM_CHUNK_SIZE = 8 * KB;

  /// The directory to cache compressed/processed static files in
  inline static std::filesystem::path STATIC_CACHE_DIR = "./.server_cache";

  /// @}

  /// @name File IO

  /// Maximum number of concurrent file ops
  inline static int IO_URING_RING_SIZE = 512;

  /// @}

  /// @name Compression

  /// The minimum size of file to be eligible for compression
  inline static int COMPRESS_MIN_BYTES = 0.5 * MB;

  /** @brief The compression level to use when compressing static files using gzip.
   *
   * -1 : default\n
   * values range from 1-9\n
   * 1 : fast but low compression\n
   * 9 : slow but high compression
   */
  inline static int STATIC_GZIP_COMPRESS_LEVEL = 1;

  /// @}

  /// @name Connection Limits

  /// The maximum number of concurrent connections before the server starts RST-ing new connections.
  inline static int CONNECTION_LIMIT = 100000;

  /// @}

private:
  inline static std::string serverName_ = "rukh/v2.9.0";
  inline static std::string serverLine_ = "server: rukh/v2.9.0\r\n";
};

} // namespace rukh
