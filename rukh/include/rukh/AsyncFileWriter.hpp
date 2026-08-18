#pragma once

#include <filesystem>

#include <rukh/core/Awaitables.hpp>

namespace rukh {

class AsyncFileWriter {
public:
  AsyncFileWriter() : fd_(-1) {}
  ~AsyncFileWriter() {
    if (fd_ != -1)
      ::close(fd_);
  }
  AsyncFileWriter(AsyncFileWriter &&other) {
    fd_ = other.fd_;
    offset_ = other.offset_;
    other.fd_ = -1;
  }
  AsyncFileWriter &operator=(AsyncFileWriter &&other) {
    if (fd_ != -1)
      ::close(fd_);
    fd_ = other.fd_;
    offset_ = other.offset_;
    other.fd_ = -1;
    return *this;
  }
  AsyncFileWriter(AsyncFileWriter const &) = delete;
  AsyncFileWriter &operator=(AsyncFileWriter const &) = delete;

  static std::optional<AsyncFileWriter> open(const std::filesystem::path &path) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1)
      return std::nullopt;
    return AsyncFileWriter(fd);
  }

  core::Task<bool> writeAll(std::string_view data) {
    int n = co_await FileWriteAwaitable{.fd = fd_, .buf = data.data(), .len = data.size()};
    co_return n == static_cast<int>(data.size());
  }

  core::Task<bool> writeChunk(std::string_view data) {
    int n = co_await FileWriteAwaitable{.fd = fd_, .buf = data.data(), .len = data.size(), .offset = offset_};
    if (n < 0)
      co_return false;
    offset_ += n;
    co_return true;
  }

private:
  int fd_ = -1;
  uint64_t offset_ = 0;
  AsyncFileWriter(int fd) : fd_(fd) {}
};
} // namespace rukh
