#pragma once

#include <coroutine>
#include <exception>
#include <optional>

#include <rukh/core/ExecutorContext.hpp>

namespace rukh {

template <typename T> class Task {
public:
  struct FinalAwaiter;

  struct promise_type {
    std::optional<T> value;
    std::coroutine_handle<> continuation;
    std::exception_ptr ex;

    Task get_return_object() { return Task{Handle::from_promise(*this)}; }
    std::suspend_always initial_suspend() { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }
    void return_value(T val) { value = std::move(val); }
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

  T await_resume() {
    if (handle_.promise().ex)
      std::rethrow_exception(handle_.promise().ex);
    return std::move(*handle_.promise().value);
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
} // namespace rukh
