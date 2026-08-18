/**
 * @file Socket.hpp
 * @brief RAII wrapper for @c socket
 */

#pragma once

namespace rukh::net {

/// RAII wrapper for @c socket
class Socket final {
public:
  Socket(Socket const &) = delete;
  Socket &operator=(Socket const &) = delete;

  Socket();

  Socket(int fd);

  Socket(Socket &&other) noexcept;

  Socket &operator=(Socket &&other) noexcept;

  ~Socket() noexcept;

  int getFd() const noexcept;
  bool isValid() const noexcept;

  explicit operator bool() const noexcept;

  int release() noexcept;

  /// sets the O_NONBLOCK flag. This is what allows us to do non-blocking I/O
  int setNonBlocking();

private:
  int socket_fd_;
  void close_socket();
};
} // namespace rukh::net
