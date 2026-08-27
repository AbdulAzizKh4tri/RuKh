/**
 * @file AsyncFileReader.hpp
 * @brief Asynchronous file reading via io_uring
 */

#pragma once

#include <coroutine>
#include <filesystem>

#include <rukh/core/Executor.hpp>

namespace rukh::core {

/// Awaitable for AsyncFileReader to read from a file using IoUringInstance
struct FileReadAwaitable {
  int fd;
  void *buf;
  size_t len;
  uint64_t offset = (uint64_t)-1; // use file offset by default
  int result = 0;

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) noexcept {
    core::tl_executor->submitFileRead(fd, buf, len, h, &result, offset);
  }

  int await_resume() { return result; }
};

/// Asynchronous file reading via io_uring
class AsyncFileReader {
public:
  AsyncFileReader() : fd_(-1) {}
  AsyncFileReader(int fd, bool owns) : fd_(fd), ownsFd_(owns) {}
  AsyncFileReader(int fd, size_t fileSize, bool owns) : fd_(fd), fileSize_(fileSize), ownsFd_(owns) {}

  ~AsyncFileReader() {
    if (ownsFd_ and fd_ != -1)
      ::close(fd_);
  }

  AsyncFileReader(AsyncFileReader &&other) {
    fd_ = other.fd_;
    fileSize_ = other.fileSize_;
    ownsFd_ = other.ownsFd_;
    offset_ = other.offset_;
    other.fd_ = -1;
  }

  AsyncFileReader &operator=(AsyncFileReader &&other) {
    fd_ = other.fd_;
    ownsFd_ = other.ownsFd_;
    fileSize_ = other.fileSize_;
    offset_ = other.offset_;
    other.fd_ = -1;
    return *this;
  }

  AsyncFileReader(AsyncFileReader const &) = delete;
  AsyncFileReader &operator=(AsyncFileReader const &) = delete;

  /**
   * @brief Open a file for reading
   * @param path The path to the file
   * @returns An AsyncFileReader on success, nullopt on failure.
   *
   */
  static std::optional<AsyncFileReader> open(const std::filesystem::path &path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1)
      return std::nullopt;
    return AsyncFileReader(fd, std::filesystem::file_size(path), true);
  }

  /**
   * @brief Read the entire file into a string.
   * @returns A string with the contents of the file
   */
  Task<std::string> readAll() {
    if (not fileSize_)
      throw std::runtime_error("readAll(): File size not set");

    std::string buf;
    buf.reserve(*fileSize_);
    int n = co_await FileReadAwaitable{.fd = fd_, .buf = buf.data(), .len = *fileSize_, .offset = offset_};
    if (n < 0)
      throw std::runtime_error("readAll(): File Read Awaitable failed");
    buf.resize(n);
    co_return buf;
  }

  /**
   * @brief Read the entire file into a passed buffer.
   */
  Task<void> readAllInto(std::vector<unsigned char> &buf) {
    if (not fileSize_)
      throw std::runtime_error("readAllInto(): File size not set");

    buf.reserve(*fileSize_);
    int n = co_await FileReadAwaitable{.fd = fd_, .buf = buf.data(), .len = *fileSize_, .offset = offset_};
    if (n < 0)
      throw std::runtime_error("readAllInto(): File Read Awaitable failed");
    buf.resize(n);
  }

  /**
   * @brief Read a chunk of the file into a string.
   * @param size The number of bytes to read
   * @returns A string of @p size bytes, nullopt if nothing left to read.
   */
  Task<std::optional<std::string>> readChunk(size_t size) {
    std::string buf;
    buf.reserve(size);
    int n = co_await FileReadAwaitable{.fd = fd_, .buf = buf.data(), .len = size, .offset = offset_};
    if (n < 0)
      throw std::runtime_error("readChunk(): File Read Awaitable failed");
    if (n == 0)
      co_return std::nullopt;
    offset_ += n;
    buf.resize(n);
    co_return buf;
  }

  /**
   * @brief Read a chunk of the file into buf.
   * @returns A of size bytes, nullopt if nothing left to read.
   */
  Task<bool> readChunkInto(std::span<unsigned char> buf) {
    size_t size = buf.size();
    int n = co_await FileReadAwaitable{.fd = fd_, .buf = buf.data(), .len = size, .offset = offset_};
    if (n < 0)
      throw std::runtime_error("readChunk(): File Read Awaitable failed");
    if (n < size)
      co_return false;
    offset_ += n;
    co_return true;
  }

  /// set file read offset
  void seek(uint64_t offset) { offset_ = offset; }

  void setFileSize(uintmax_t size) { fileSize_ = size; }

private:
  int fd_ = -1;
  bool ownsFd_ = false;
  std::optional<uintmax_t> fileSize_;
  uint64_t offset_ = 0;
};
} // namespace rukh::core
