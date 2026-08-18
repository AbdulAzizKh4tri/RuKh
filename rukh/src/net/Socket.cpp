#include <rukh/net/Socket.hpp>

#include <fcntl.h>
#include <unistd.h>

namespace rukh::net {

Socket::Socket() { socket_fd_ = -1; }

Socket::Socket(int fd) : socket_fd_(fd) {}

Socket::Socket(Socket &&other) noexcept : socket_fd_(other.socket_fd_) { other.socket_fd_ = -1; }

Socket &Socket::operator=(Socket &&other) noexcept {
  if (this == &other)
    return *this;
  close_socket();
  socket_fd_ = other.socket_fd_;
  other.socket_fd_ = -1;
  return *this;
}

Socket::~Socket() noexcept { close_socket(); }

int Socket::getFd() const noexcept { return socket_fd_; }
bool Socket::isValid() const noexcept { return socket_fd_ != -1; }

Socket::operator bool() const noexcept { return socket_fd_ != -1; }

int Socket::release() noexcept {
  int fd = socket_fd_;
  socket_fd_ = -1;
  return fd;
}

int Socket::setNonBlocking() {
  int flags = fcntl(socket_fd_, F_GETFL, 0);
  return fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
}

void Socket::close_socket() {
  if (socket_fd_ != -1) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
}
} // namespace rukh
