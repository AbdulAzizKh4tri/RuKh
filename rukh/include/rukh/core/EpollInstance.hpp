/**
 * @file EpollInstance.hpp
 * @brief RAII wrapper for epoll
 */

#pragma once

#include <sys/epoll.h>

namespace rukh::core {

/// RAII wrapper for @c epoll
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

  /// Release fd from RAII
  int release() noexcept;

  /// Add fd to epoll with events such as (EPOLLIN | EPOLLET). check man epoll_event for more.
  int add(int fd, uint32_t events, int data);

  /// Two syscalls, use the add / modify methods if possible.
  int addOrModify(int fd, uint32_t events, int data);

  /// Modify fd events
  int modify(int fd, uint32_t events, int data);

  /// Remove fd tracking from epoll
  int remove(int fd);

  /**
   * @brief Wait for events
   * @param events An array of @c epoll_events to be filled
   * @param maxevents The maximum number of events to be returned
   * @param timeout The maximum wait time in milliseconds
   */
  int wait(epoll_event *events, int maxevents, int timeout = -1);

private:
  int epoll_fd_ = -1;

  void close_epoll_instance() noexcept;
};
} // namespace rukh::core
