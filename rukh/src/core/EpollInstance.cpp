#include <rukh/core/EpollInstance.hpp>

#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <unistd.h>
namespace rukh {

EpollInstance::EpollInstance() {
  epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd_ == -1) {
    SPDLOG_CRITICAL("ERROR: {}", strerror(errno));
    throw std::runtime_error("Failed to create epoll instance");
  }
}

EpollInstance::EpollInstance(EpollInstance &&other) noexcept : epoll_fd_(other.epoll_fd_) { other.epoll_fd_ = -1; }

EpollInstance &EpollInstance::operator=(EpollInstance &&other) noexcept {
  if (this == &other)
    return *this;
  close_epoll_instance();
  epoll_fd_ = other.epoll_fd_;
  other.epoll_fd_ = -1;
  return *this;
}

EpollInstance::~EpollInstance() noexcept { close_epoll_instance(); }

int EpollInstance::getFd() const noexcept { return epoll_fd_; }
bool EpollInstance::isValid() const noexcept { return epoll_fd_ != -1; }

EpollInstance::operator bool() const noexcept { return epoll_fd_ != -1; }

int EpollInstance::release() noexcept {
  int fd = epoll_fd_;
  epoll_fd_ = -1;
  return fd;
}

int EpollInstance::add(int fd, uint32_t events, int data) {
  epoll_event event = {};
  event.events = events;
  event.data.fd = data;
  int ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
  if (ret == -1) {
    SPDLOG_CRITICAL("ERROR (fd: {}): {}", fd, strerror(errno));
    throw std::runtime_error("Failed to add fd " + std::to_string(fd) + " to epoll");
  }
  return ret;
}

int EpollInstance::addOrModify(int fd, uint32_t events, int data) {
  epoll_event event = {};
  event.events = events;
  event.data.fd = data;
  int ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
  if (ret == -1 && errno == EEXIST)
    return modify(fd, events, data);
  if (ret == -1)
    throw std::runtime_error("Failed to add/modify fd " + std::to_string(fd));
  return ret;
}

int EpollInstance::modify(int fd, uint32_t events, int data) {
  epoll_event event = {};
  event.events = events;
  event.data.fd = data;

  int ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event);
  if (ret == -1) {
    SPDLOG_CRITICAL("ERROR (fd: {}): {}", fd, strerror(errno));
    throw std::runtime_error("Failed to modify epoll, fd=" + std::to_string(fd));
  }
  return ret;
}

int EpollInstance::remove(int fd) {
  int ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
  if (ret == -1) {
    if (errno == ENOENT) {
      return 0;
    }
    SPDLOG_CRITICAL("ERROR (fd: {}): {}", fd, strerror(errno));
    throw std::runtime_error("Failed to remove fd " + std::to_string(fd) + " from epoll");
  }
  return ret;
}

int EpollInstance::wait(epoll_event *events, int maxevents, int timeout) {
  for (;;) {
    int numEvents = ::epoll_wait(epoll_fd_, events, maxevents, timeout);
    if (numEvents == -1) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("Failed to wait for events");
    }
    return numEvents;
  }
}

std::vector<epoll_event> EpollInstance::wait(int maxevents, int timeout) {
  if (maxevents <= 0)
    return {};
  std::vector<epoll_event> events(maxevents);
  int numEvents = wait(events.data(), maxevents, timeout);
  events.resize(numEvents);
  return events;
}

void EpollInstance::close_epoll_instance() noexcept {
  if (epoll_fd_ != -1) {
    ::close(epoll_fd_);
    epoll_fd_ = -1;
  }
}
} // namespace rukh
