/**
 * @file PoolJob.hpp
 * @brief Represents a single job in the thread pool, stores the callable and state
 */

#pragma once

#include <rukh/pool/IPoolJob.hpp>
#include <rukh/pool/PoolJobState.hpp>

namespace rukh::pool {

/**
 * @brief Represents a single job in the thread pool, stores the callable and shares the state with PoolJobAwaitable.
 * @tparam F callable
 *
 *
 * @see IPoolJob
 * @see ThreadPool
 * @see PoolJobAwaitable
 * @see PoolJobState
 */
template <typename F> struct PoolJob : IPoolJob {
  using R = std::invoke_result_t<F>;

  F callable;
  std::shared_ptr<PoolJobState<R>> state;
  std::shared_ptr<PoolJob> self;

  PoolJob(F f, std::shared_ptr<PoolJobState<R>> s) : callable(std::move(f)), state(std::move(s)) {}

  PoolJob(const PoolJob &) = delete;
  PoolJob &operator=(const PoolJob &) = delete;

  /**
   * @brief Runs the job, stores results in state.
   * If the job is being co_awaited, notifies the executor upon completion
   *
   * @see PoolJobAwaitable::await_suspend
   * @see PoolJobState
   */
  void runJob() override {
    try {
      if constexpr (std::is_void_v<R>)
        callable();
      else
        state->result = callable();
    } catch (...) {
      state->exception = std::current_exception();
    }
    state->done = true;
    if (state->executor and state->callerSetOrJobComplete.exchange(true)) {
      state->executor->post(state->caller);
    }
    self.reset();
  }
};
} // namespace rukh::pool
