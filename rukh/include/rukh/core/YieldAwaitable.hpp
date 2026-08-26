/**
 * @file YieldAwaitable.hpp
 * \todo docs
 */

#pragma once

#include <coroutine>

#include <rukh/core/Executor.hpp>

namespace rukh::core {

struct YieldAwaitable {
  std::chrono::steady_clock::time_point deadline;

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) const noexcept { core::tl_executor->wakeMe(h); }

  void await_resume() const noexcept {}
};

} // namespace rukh::core
