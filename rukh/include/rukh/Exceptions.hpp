#pragma once

#include <stdexcept>

namespace rukh {

struct ServerException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  int status_code;
  bool fatal;

  explicit ServerException(const std::string &msg, int code = 500, bool fatal = true)
      : std::runtime_error(msg), status_code(code), fatal(fatal) {}
};

struct HandlerException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  int status_code;
  bool fatal;

  explicit HandlerException(const std::string &msg, int code = 500, bool fatal = false)
      : std::runtime_error(msg), status_code(code), fatal(fatal) {}
};

struct ConnectionException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  explicit ConnectionException(const std::string &msg) : std::runtime_error(msg) {}
};

struct DatabaseException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  explicit DatabaseException(const std::string &msg) : std::runtime_error(msg) {}
};

struct OrmException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  explicit OrmException(const std::string &msg) : std::runtime_error(msg) {}
};

} // namespace rukh
