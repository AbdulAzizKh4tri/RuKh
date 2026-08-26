/**
 * @file SpliceAwaitable
 */

#pragma once

#include <coroutine>

#include <rukh/core/Executor.hpp>

namespace rukh::core {

struct SpliceAwaitable {
  int readFd;
  off_t readOffset;

  int writeFd;
  off_t writeOffset;

  size_t len;
  int result;

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) noexcept {
    core::tl_executor->submitSplice(readFd, readOffset, writeFd, writeOffset, len, h, &result);
  }

  int await_resume() const noexcept { return result; }
};

} // namespace rukh::core
