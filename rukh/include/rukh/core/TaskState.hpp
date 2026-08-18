#pragma once

#include <coroutine>

#include <rukh/core/Executor.hpp>

namespace rukh {

template <typename R> struct TaskState {
  std::optional<R> result;
  std::exception_ptr exception;
  std::coroutine_handle<> caller;
  core::Executor *executor = nullptr;
  std::atomic<bool> done = false;
  std::atomic<bool> callerSet = false;
};

template <> struct TaskState<void> {
  std::exception_ptr exception;
  std::coroutine_handle<> caller;
  core::Executor *executor = nullptr;
  std::atomic<bool> done = false;
  std::atomic<bool> callerSet = false;
};
} // namespace rukh
