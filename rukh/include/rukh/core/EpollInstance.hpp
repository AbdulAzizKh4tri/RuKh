/**
 * @file EpollInstance.hpp
 * @brief RAII wrapper for epoll
 */

#pragma once

#include <sys/epoll.h>

namespace rukh::core {

struct EpollException : public std::runtime_error {
  using std::runtime_error::runtime_error;
  explicit EpollException(const std::string &msg) : std::runtime_error(msg) {}
};

/// RAII wrapper for epoll
class EpollInstance final {
public:
  EpollInstance(EpollInstance const &) = delete;
  EpollInstance &operator=(EpollInstance const &) = delete;

  EpollInstance();
  EpollInstance(EpollInstance &&other) noexcept;
  EpollInstance &operator=(EpollInstance &&other) noexcept;

  ~EpollInstance() noexcept;

  int getFd() const noexcept;
  bool isValid() const noexcept;

  explicit operator bool() const noexcept;

  int release() noexcept;

  int add(int fd, uint32_t events, int data);

  int addOrModify(int fd, uint32_t events, int data);

  int modify(int fd, uint32_t events, int data);

  int remove(int fd);

  int wait(epoll_event *events, int maxevents, int timeout = -1);

private:
  int epoll_fd_ = -1;

  void close_epoll_instance() noexcept;
};
} // namespace rukh::core
