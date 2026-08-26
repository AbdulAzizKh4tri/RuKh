/**
 * @file SocketAwaitables.hpp
 * @brief Awaitables for sockets
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

} // namespace rukh::core
