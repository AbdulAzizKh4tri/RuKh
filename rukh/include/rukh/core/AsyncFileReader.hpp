/**
 * @file AsyncFileReader.hpp
 * @brief Asynchronous file reading via io_uring
 */

#pragma once

#include <filesystem>

#include <rukh/core/Awaitables.hpp>

namespace rukh::core {

/// Asynchronous file reading via io_uring
class AsyncFileReader {
public:
  AsyncFileReader() : fd_(-1) {}
  ~AsyncFileReader() {
    if (fd_ != -1)
      ::close(fd_);
  }

  AsyncFileReader(AsyncFileReader &&other) {
    fd_ = other.fd_;
    fileSize_ = other.fileSize_;
    offset_ = other.offset_;
    other.fd_ = -1;
  }

  AsyncFileReader &operator=(AsyncFileReader &&other) {
    if (fd_ != -1)
      ::close(fd_);
    fd_ = other.fd_;
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
   * Use \ref AsyncFileReader::open(const std::filesystem::path &path, const uintmax_t fileSize) if you know the file
   * size. Prevents extra work.
   */
  static std::optional<AsyncFileReader> open(const std::filesystem::path &path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1)
      return std::nullopt;
    return AsyncFileReader(fd, std::filesystem::file_size(path));
  }

  /**
   * @brief Open a file for reading
   * @param path The path to the file
   * @param fileSize The size of the file
   * @returns An AsyncFileReader on success, nullopt on failure.
   */
  static std::optional<AsyncFileReader> open(const std::filesystem::path &path, const uintmax_t fileSize) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1)
      return std::nullopt;
    return AsyncFileReader(fd, fileSize);
  }

  /**
   * @brief Read the entire file into a string.
   * @returns A string with the contents of the file
   */
  Task<std::string> readAll() {
    std::string buf(fileSize_, '\0');
    int n = co_await FileReadAwaitable{.fd = fd_, .buf = buf.data(), .len = fileSize_, .offset = offset_};
    if (n < 0)
      throw std::runtime_error("File Read Awaitable failed");
    buf.resize(n);
    co_return buf;
  }

  /**
   * @brief Read a chunk of the file into a string.
   * @param size The number of bytes to read
   * @returns A string of upto @p size bytes, nullopt if nothing left to read.
   */
  Task<std::optional<std::string>> readChunk(size_t size) {
    std::string buf(size, '\0');
    int n = co_await FileReadAwaitable{.fd = fd_, .buf = buf.data(), .len = size, .offset = offset_};
    if (n < 0)
      throw std::runtime_error("File Read Awaitable failed");
    if (n == 0)
      co_return std::nullopt;
    offset_ += n;
    buf.resize(n);
    co_return buf;
  }

  /// set file read offset
  void seek(uint64_t offset) { offset_ = offset; }

private:
  int fd_ = -1;
  uintmax_t fileSize_;
  uint64_t offset_ = 0;

  AsyncFileReader(int fd) : fd_(fd) {}
  AsyncFileReader(int fd, uintmax_t fileSize) : fd_(fd), fileSize_(fileSize) {}
};
} // namespace rukh::core
