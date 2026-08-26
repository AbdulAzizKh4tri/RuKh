/**
 * @file AsyncFileWriter.hpp
 * @brief Asynchronous file writing via io_uring
 */

#pragma once

#include <coroutine>
#include <filesystem>

#include <rukh/core/Executor.hpp>

namespace rukh::core {

/// Awaitable for AsyncFileWriter to write to a file using IoUringInstance
struct FileWriteAwaitable {
  int fd;
  const void *buf;
  size_t len;
  uint64_t offset = (uint64_t)-1; // use file offset by default
  int result = 0;

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) noexcept {
    core::tl_executor->submitFileWrite(fd, buf, len, h, &result, offset);
  }

  int await_resume() { return result; }
};

/// Asynchronous file writing via io_uring
class AsyncFileWriter {
public:
  AsyncFileWriter() : fd_(-1) {}
  AsyncFileWriter(int fd) : fd_(fd) {}

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

  /**
   * @brief Open a file for writing
   * @param path The path to the write to.
   * @returns An AsyncFileWriter on success, nullopt on failure.
   */
  static std::optional<AsyncFileWriter> open(const std::filesystem::path &path) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1)
      return std::nullopt;
    return AsyncFileWriter(fd);
  }

  /// Write the entire contents of @p data into the file.
  Task<bool> writeAll(std::string_view data) {
    int n = co_await FileWriteAwaitable{.fd = fd_, .buf = data.data(), .len = data.size()};
    co_return n == static_cast<int>(data.size());
  }

  /// @brief Write a chunk of data into the file. May return without writing full data to the file. Next call carries on
  /// from where it left.
  Task<bool> writeChunk(std::string_view data) {
    int n = co_await FileWriteAwaitable{.fd = fd_, .buf = data.data(), .len = data.size(), .offset = offset_};
    if (n < 0)
      co_return false;
    offset_ += n;
    co_return true;
  }

private:
  int fd_ = -1;
  uint64_t offset_ = 0;
};
} // namespace rukh::core
