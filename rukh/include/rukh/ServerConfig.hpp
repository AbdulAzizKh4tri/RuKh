#pragma once

#include <string>
#include <stdexcept>
#include <filesystem>

namespace rukh {

class ServerConfig {
public:
  // ── Server identity ───────────────────────────────────────
  static void setServerName(const std::string &name) {
    if (name.empty())
      throw std::runtime_error("Server name cannot be empty");
    serverName_ = name;
    serverLine_ = "server: " + name + "\r\n";
  }
  static const std::string &getServerName() { return serverName_; }
  static const std::string &getServerLine() { return serverLine_; }

  // ── Timeouts (seconds) ────────────────────────────────────
  inline static int INACTIVITY_TIMEOUT_S = 10;
  inline static int FORMATION_TIMEOUT_S = 120;
  inline static int GRACEFUL_SHUTDOWN_TIMEOUT_S = 20;
  inline static int EPOLL_WAIT_TIMEOUT = 1;

  // ── Request/Response limits ───────────────────────────────
  inline static size_t MAX_HEADER_BYTES = 8 * 1024;
  inline static size_t MAX_CONTENT_LENGTH = 1 * 1024 * 1024;
  inline static size_t MAX_WRITE_BUFFER_BYTES = 10 * 1024 * 1024;
  inline static size_t MAX_TE_CHUNK_LENGTH = 1 * 1024 * 1024;
  inline static size_t MAX_TE_LENGTH = 10 * 1024 * 1024;

  // ── Multipart ─────────────────────────────────────────────
  inline static size_t MULTIPART_BUFFER_SIZE = 1024;
  inline static size_t MAX_MULTIPART_CONTENT_LENGTH = 50 * 1024 * 1024;
  inline static size_t MAX_MULTIPART_TE_LENGTH = 50 * 1024 * 1024;
  inline static size_t MAX_MULTIPART_FIELD_SIZE = 64 * 1024;
  inline static size_t MAX_MULTIPART_FILE_SIZE = 20 * 1024 * 1024;
  inline static size_t MAX_MULTIPART_PARTS = 256;
  inline static size_t MAX_MULTIPART_BOUNDARY_LENGTH = 64;

  // ── Static File Serving ───────────────────────────────────
  inline static size_t STATIC_STREAM_THRESHOLD_BYTES = 1 * 1024;
  inline static size_t STATIC_STREAM_CHUNK_SIZE = 4096;
  inline static std::filesystem::path STATIC_CACHE_DIR = "./.server_cache";

  // ── File IO ───────────────────────────────────────────────
  inline static int IO_URING_RING_SIZE = 64;

  // ── Compression ───────────────────────────────────────────
  inline static int COMPRESS_MIN_BYTES = 256;
  inline static int STATIC_COMPRESS_LEVEL = 9;

  // ── Connection limits ─────────────────────────────────────
  inline static int CONNECTION_LIMIT = 100000;

private:
  inline static std::string serverName_ = "rukh/v2.8.20";
  inline static std::string serverLine_ = "server: rukh/v2.8.20\r\n";
};

} // namespace rukh
