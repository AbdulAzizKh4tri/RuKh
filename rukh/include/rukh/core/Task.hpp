/**
 * @file Task.hpp
 * @brief A coroutine task that can be co_awaited.
 */

#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

#include <rukh/core/ExecutorContext.hpp>

namespace rukh::core {

/**
 * @brief A coroutine task that can be co_awaited.
 * @tparam T  The type produced by the coroutine.
 * @details
 * `Task` is the primary coroutine type used throughout the rukh runtime.
 * Typical usage:
 * @code
 * Task<int> compute() {
 *     co_return 42;
 * }
 *
 * Task<void> caller() {
 *     int result = co_await compute();
 *     // ...
 * }
 * @endcode
 * @note coroutines (aka functions that `co_return` `Task<>`) generally do not run until co_awaited.
 *
 * @see Task<void>
 */
template <typename T> class [[nodiscard("Task doesn't run until co_awaited")]] Task {
public:
  struct FinalAwaiter;

  /// promise_type is required by std::coroutine_traits<> for all Types that are returned by a coroutine.
  /// Basically, "I'm a coroutine, and here's what to do at each step when running me."
  struct promise_type {
    std::optional<T> value;
    std::coroutine_handle<> continuation;
    std::exception_ptr ex;

    /// returning Task with the coroutine handle built with *this promise_type object.
    Task get_return_object() { return Task{Handle::from_promise(*this)}; }

    /// Always pause the coroutine before running the first line, i.e, run only when co_awaited.
    std::suspend_always initial_suspend() { return {}; }

    /// What to do after the final line of the coroutine (or at a co_return).
    FinalAwaiter final_suspend() noexcept { return {}; }

    /// What to do with co_return val. (store it in the promise_type, we handle this later).
    void return_value(T val) { value = std::move(val); }

    /// same but for exceptions
    void unhandled_exception() { ex = std::current_exception(); }
  };

  using Handle = std::coroutine_handle<promise_type>;

  struct FinalAwaiter {

    /// @brief always false aka run await_suspend(). It handles what to do after this coroutine is finished.
    bool await_ready() noexcept { return false; }

    /// @brief If there's a continuation, resume it. Otherwise notify the Executor that we're done, and it's safe to
    /// destroy this coroutine.
    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
      if (handle.promise().continuation)
        return handle.promise().continuation;
      notifyTaskFinished(handle);
      return std::noop_coroutine();
    }

    /// @brief Has to exist to satisfy the Awaitable interface. Never actually reached.
    /// await_suspend always transfers away (to continuation or noop_coroutine),
    /// so control never returns here.
    void await_resume() noexcept { std::unreachable(); }
  };

  Task() : handle_(nullptr) {}

  explicit Task(Handle h) : handle_(h) {}

  Task(Task &&other) noexcept : handle_(other.handle_) { other.handle_ = {}; }

  Task &operator=(Task &&other) noexcept {
    if (this == &other)
      return *this;
    if (handle_)
      handle_.destroy();
    handle_ = other.handle_;
    other.handle_ = {};
    return *this;
  }

  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;

  /// Scope/Lifetime of coroutine types should not be thought of like normal objects.
  ~Task() {
    if (handle_)
      handle_.destroy(); // This is where the coroutine finally dies.
  }

  /**
   * @name coroutine_traits: The methods below allow the core::Task<> itself to be co_await-ed.
   * @{
   */

  /// @brief Always false, a freshly-returned Task is always parked at initial_suspend,
  /// so the awaiter never has a synchronously-ready value to skip straight to.
  bool await_ready() { return false; }

  // (Returning a coroutine_handle from await_suspend = tail-resume it directly, instead of unwinding back to
  // whoever called .resume() on the caller, which in our case is the Executor::spawn().)
  /// @brief Store the caller so FinalAwaiter can resume it later, then symmetric-transfer into this coroutine's
  /// handle.
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) {
    handle_.promise().continuation = caller;
    return handle_;
  }

  /// return the value stored in the promise_type (or throw the exception to the caller).
  T await_resume() {
    if (handle_.promise().ex)
      std::rethrow_exception(handle_.promise().ex);
    return std::move(*handle_.promise().value);
  }
  /** @} */

  /// helpers for the Executor
  bool done() const { return handle_.done(); }

  bool resume() {
    if (!handle_ || handle_.done())
      return false;
    handle_.resume();
    return true;
  }

  std::coroutine_handle<> handle() const { return handle_; }

private:
  Handle handle_;
};

/**
 * @brief Specialisation of Task for coroutines that return void.
 * @see Task
 */
template <> class Task<void> {
public:
  struct FinalAwaiter;

  struct promise_type {
    std::coroutine_handle<> continuation;
    std::exception_ptr ex;

    Task get_return_object() { return Task{Handle::from_promise(*this)}; }
    std::suspend_always initial_suspend() { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { ex = std::current_exception(); }
  };

  using Handle = std::coroutine_handle<promise_type>;

  struct FinalAwaiter {
    bool await_ready() noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
      if (handle.promise().continuation)
        return handle.promise().continuation;
      notifyTaskFinished(handle);
      return std::noop_coroutine();
    }

    void await_resume() noexcept {}
  };

  Task() : handle_(nullptr) {}

  explicit Task(Handle h) : handle_(h) {}

  Task(Task &&other) noexcept : handle_(other.handle_) { other.handle_ = {}; }

  Task &operator=(Task &&other) noexcept {
    if (this == &other)
      return *this;
    if (handle_)
      handle_.destroy();
    handle_ = other.handle_;
    other.handle_ = {};
    return *this;
  }

  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;

  ~Task() {
    if (handle_)
      handle_.destroy();
  }

  bool await_ready() { return false; }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) {
    handle_.promise().continuation = caller;
    return handle_;
  }

  void await_resume() {
    if (handle_.promise().ex)
      std::rethrow_exception(handle_.promise().ex);
  }

  bool done() const { return handle_.done(); }

  bool resume() {
    if (!handle_ || handle_.done())
      return false;
    handle_.resume();
    return true;
  }

  std::coroutine_handle<> handle() const { return handle_; }

private:
  Handle handle_;
};

} // namespace rukh::core
