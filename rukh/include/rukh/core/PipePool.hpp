#pragma once

#include <chrono>
#include <coroutine>
#include <deque>
#include <optional>
#include <vector>

#include <rukh/core/Executor.hpp>
#include <rukh/core/Task.hpp>

namespace rukh::core {

struct Pipe {
  int in;
  int out;
};

struct PipeWaitAwaitable {
  class PipePool *pool;
  std::chrono::steady_clock::time_point deadline;
  int64_t eventKey = 0;

  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) noexcept;
  void await_resume() const noexcept {}
};

class PipePool {
public:
  Task<std::optional<Pipe>> acquire(std::chrono::steady_clock::time_point deadline);
  void release(Pipe p);
  void discard(Pipe p);

private:
  friend struct PipeWaitAwaitable;

  std::optional<Pipe> tryAcquire();
  void wakeOneWaiter();

  std::vector<Pipe> freePipes_;
  std::deque<int64_t> waiters_;
  size_t created_ = 0;
  size_t maxPipes_ = 48;
};

inline thread_local PipePool tl_pipe_pool;

} // namespace rukh::core
