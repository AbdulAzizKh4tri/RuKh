/**
 * @file PoolJobState.hpp
 * @brief PoolJobState struct
 */

#pragma once

#include <coroutine>

#include <rukh/core/Executor.hpp>

namespace rukh::pool {

/**
 * @brief Stores the state of a @c PoolJob
 * @see ThreadPool
 * @see PoolJobAwaitable
 * @see PoolJob
 *
 */
template <typename R> struct PoolJobState {
  /// return type of the PoolJob
  std::optional<R> result;
  std::exception_ptr exception;
  /// coroutine handle to resume upon completion
  std::coroutine_handle<> caller;
  core::Executor *executor = nullptr;
  std::atomic<bool> done = false;

  /**
   * @brief Has been co_awaited and hence the caller handle has been set, or the job is complete.
   *
   * Used by @c PoolJobAwaitable::await_suspend to check if the PoolJob has finished running: notifies the executor if
   * so.
   *
   * Used by @c PoolJob::runJob() to check if the Job has been co_awaited. If not, it should not notify the
   * executor.
   */
  std::atomic<bool> callerSetOrJobComplete = false;
};

/// void return type specialization for @c PoolJobState
template <> struct PoolJobState<void> {
  std::exception_ptr exception;
  std::coroutine_handle<> caller;
  core::Executor *executor = nullptr;
  std::atomic<bool> done = false;
  std::atomic<bool> callerSetOrJobComplete = false;
};
} // namespace rukh::pool
