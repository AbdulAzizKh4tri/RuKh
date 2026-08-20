#pragma once

#include <stdexcept>

namespace rukh {

/// Exception thrown when a socket operation fails.
struct SocketException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  explicit SocketException(const std::string &msg) : std::runtime_error(msg) {}
};

/// Exception thrown when an epoll operation fails.
struct EpollException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  explicit EpollException(const std::string &msg) : std::runtime_error(msg) {}
};

/// Exception representing a server-level error.
struct ServerException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  int status_code;
  bool fatal;

  explicit ServerException(const std::string &msg, int code = 500, bool fatal = true)
      : std::runtime_error(msg), status_code(code), fatal(fatal) {}
};

/// Exception thrown when a request handler encounters an error.
struct HandlerException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  int status_code;
  bool fatal;

  explicit HandlerException(const std::string &msg, int code = 500, bool fatal = false)
      : std::runtime_error(msg), status_code(code), fatal(fatal) {}
};

/// Exception thrown when compression fails.
struct CompressorException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  explicit CompressorException(const std::string &msg) : std::runtime_error(msg) {}
};

/// Exception thrown when a connection operation fails.
struct ConnectionException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  explicit ConnectionException(const std::string &msg) : std::runtime_error(msg) {}
};

/// Exception thrown when a database operation fails.
struct DatabaseException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  explicit DatabaseException(const std::string &msg) : std::runtime_error(msg) {}
};

/// Exception thrown when an ORM operation fails.
struct OrmException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  explicit OrmException(const std::string &msg) : std::runtime_error(msg) {}
};

} // namespace rukh
