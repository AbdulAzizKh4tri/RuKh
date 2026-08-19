/**
 * @file PoolJobAwaitable.hpp
 * @brief Awaitable so we can co_await ThreadPool jobs
 */

#pragma once

#include <spdlog/spdlog.h>

#include <rukh/pool/PoolJobState.hpp>

namespace rukh::pool {

/**
 * @brief Awaitable so we can co_await ThreadPool jobs
 * @see ThreadPool
 * @see PoolJob
 * @see PoolJobState
 */
template <typename R> struct PoolJobAwaitable {
  std::shared_ptr<PoolJobState<R>> state;
  bool awaited_ = false;

  PoolJobAwaitable(std::shared_ptr<PoolJobState<R>> s) : state(std::move(s)) {}

  PoolJobAwaitable(const PoolJobAwaitable &) = delete;
  PoolJobAwaitable &operator=(const PoolJobAwaitable &) = delete;

  PoolJobAwaitable(PoolJobAwaitable &&other) noexcept : state(std::move(other.state)), awaited_(other.awaited_) {
    other.awaited_ = true;
  }

  PoolJobAwaitable &operator=(PoolJobAwaitable &&other) noexcept {
    if (this == &other)
      return *this;
    if (!awaited_)
      SPDLOG_WARN("submit() result overriden without being awaited");
    state = std::move(other.state);
    awaited_ = other.awaited_;
    other.awaited_ = true;
    return *this;
  }

  ~PoolJobAwaitable() {
    if (!awaited_)
      SPDLOG_WARN("submit() called without being awaited - use fireAndForget() instead");
  }

  /// Job may be completed by the time we co_await, suspension may or may not be required. @returns state->done.
  bool await_ready() noexcept {
    awaited_ = true;
    return state->done;
  }

  /**
   * @brief Sets the caller coroutine handle in state.
   * If the job is already finished, notify the executor.
   * If not finished, the runJob will see PoolJobState::callerSetOrJobComplete set to true and notify the executor when
   * it completes.
   *
   * @see PoolJob::runJob
   * @see PoolJobState
   */
  void await_suspend(std::coroutine_handle<> h) noexcept {
    state->caller = h;
    if (state->callerSetOrJobComplete.exchange(true)) {
      state->executor->post(h);
    }
  }

  auto await_resume() {
    if (state->exception)
      std::rethrow_exception(state->exception);
    if constexpr (not std::is_void_v<R>)
      return std::move(*state->result);
  }
};
} // namespace rukh::pool
