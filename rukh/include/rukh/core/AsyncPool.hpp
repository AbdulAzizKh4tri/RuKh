/**
 * @file AsyncPool.hpp
 * @brief A bounded pool of T* whose acquire() suspends the calling coroutine
 */
#pragma once

#include <coroutine>
#include <deque>
#include <mutex>
#include <vector>

#include <rukh/core/Executor.hpp>
#include <rukh/core/Task.hpp>

namespace rukh::core {

/**
 * @brief A bounded pool of T* whose acquire() suspends the calling coroutine
 * (never blocks the reactor thread) when no item is free.
 */
template <typename T> class AsyncPool {
public:
  explicit AsyncPool(std::vector<T *> items) : available_(std::move(items)) {}

  core::Task<T *> acquire() {
    std::unique_lock lock(mutex_);
    if (!available_.empty()) {
      T *item = available_.back();
      available_.pop_back();
      co_return item;
    }
    T *item = co_await AcquireAwaitable{this, std::move(lock)};
    co_return item;
  }

  /**
   * @brief Never blocks. Hands the item straight to a waiter if one exists,
   * otherwise returns it to the free list.
   */
  void release(T *item) {
    std::unique_lock lock(mutex_);
    if (!waiters_.empty()) {
      Waiter w = std::move(waiters_.front());
      waiters_.pop_front();
      lock.unlock();
      *w.slot = item;
      w.executor->post(w.handle);
      return;
    }
    available_.push_back(item);
  }

private:
  struct Waiter {
    std::coroutine_handle<> handle;
    core::Executor *executor;
    T **slot;
  };

  struct AcquireAwaitable {
    AsyncPool *pool;
    std::unique_lock<std::mutex> lock; // held from acquire()'s check, released in await_suspend
    T *result = nullptr;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
      // Re-check under the same lock acquire() took — closes the race where
      // a release() lands between acquire()'s "empty" check and this suspend.
      if (!pool->available_.empty()) {
        result = pool->available_.back();
        pool->available_.pop_back();
        lock.unlock();
        h.resume(); // already have an item, resume immediately, no real suspension
        return;
      }
      pool->waiters_.push_back({h, core::tl_executor, &result});
      lock.unlock();
    }

    T *await_resume() const noexcept { return result; }
  };

  std::mutex mutex_;
  std::vector<T *> available_;
  std::deque<Waiter> waiters_;
};

} // namespace rukh::core
