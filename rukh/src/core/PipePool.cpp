#include <rukh/core/PipePool.hpp>

#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include <rukh/ServerConfig.hpp>

namespace rukh::core {

void PipeWaitAwaitable::await_suspend(std::coroutine_handle<> h) noexcept {
  eventKey = core::tl_executor->waitForEvent(h, deadline);
  pool->waiters_.push_back(eventKey);
}

Task<std::optional<Pipe>> PipePool::acquire(std::chrono::steady_clock::time_point deadline) {
  for (;;) {
    if (auto p = tryAcquire())
      co_return p;

    co_await PipeWaitAwaitable{this, deadline};
    if (core::tl_timed_out)
      co_return std::nullopt;
    // else: genuinely fired via wakeOneWaiter, retry
  }
}

std::optional<Pipe> PipePool::tryAcquire() {
  if (!freePipes_.empty()) {
    Pipe p = freePipes_.back();
    freePipes_.pop_back();
    return p;
  }
  if (created_ >= maxPipes_)
    return std::nullopt;

  int fds[2];
  if (::pipe2(fds, O_NONBLOCK) < 0)
    return std::nullopt;

  if (ServerConfig::PIPE_BUFFER_SIZE != 64 * 1024) {
    if (fcntl(fds[1], F_SETPIPE_SZ, ServerConfig::PIPE_BUFFER_SIZE) < 0) {
      static std::once_flag warned;
      std::call_once(warned, [] { SPDLOG_WARN("F_SETPIPE_SZ failed — check /proc/sys/fs/pipe-user-pages-soft"); });
    }
  }
  created_++;
  return Pipe{fds[1], fds[0]};
}

void PipePool::release(Pipe p) {
  freePipes_.push_back(p);
  wakeOneWaiter();
}

void PipePool::discard(Pipe p) {
  ::close(p.in);
  ::close(p.out);
  created_--;
  wakeOneWaiter();
}

void PipePool::wakeOneWaiter() {
  if (waiters_.empty())
    return;
  int64_t key = waiters_.front();
  waiters_.pop_front();
  core::tl_executor->fireEvent(key);
}

} // namespace rukh::core
