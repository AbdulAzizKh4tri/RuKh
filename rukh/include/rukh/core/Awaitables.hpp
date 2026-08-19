/**
 * @file Awaitables.hpp
 * @brief Awaitables for async I/O
 */

#pragma once

#include <coroutine>

#include <rukh/core/Executor.hpp>

namespace rukh::core {

/// Awaitable for an fd to become readable within a deadline
struct ReadAwaitable {
  int fd;
  std::chrono::steady_clock::time_point deadline;

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) const noexcept { core::tl_executor->waitForRead(fd, h, deadline); }

  void await_resume() const noexcept {}
};

/// Awaitable for an fd to become writable within a deadline
struct WriteAwaitable {
  int fd;
  std::chrono::steady_clock::time_point deadline;

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) const noexcept { core::tl_executor->waitForWrite(fd, h, deadline); }

  void await_resume() const noexcept {}
};

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
} // namespace rukh::core
