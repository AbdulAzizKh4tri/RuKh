#include <rukh/core/Executor.hpp>

#include <spdlog/spdlog.h>
#include <sys/eventfd.h>

#include <rukh/ServerConfig.hpp>
#include <rukh/core/ExecutorContext.hpp>
#include <rukh/core/utils.hpp>

namespace rukh::core {

thread_local Executor *tl_executor = nullptr;
thread_local bool tl_timed_out = false;
void notifyTaskFinished(std::coroutine_handle<> h) noexcept {
  if (tl_executor)
    tl_executor->markRootFinished(h.address());
}

Executor::Executor() {
  eventFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (eventFd_ < 0)
    throw std::runtime_error("eventfd failed");
  registerReadFd(eventFd_);
}

void Executor::spawn(core::Task<void> task) {
  auto h = task.handle();
  if (not h)
    return;

  ownedTaskMap_[h.address()] = ownedTasks_.size();
  ownedTasks_.push_back(std::move(task));
  readyQueue_.push({h, false});
}

void Executor::unregister(int fd) {
  writeInterested_.erase(fd);
  epoll_.remove(fd);
  suspendedTasks_.erase(fd);
}

void Executor::post(std::coroutine_handle<> h) {
  {
    std::unique_lock lock(poolResumeQueueMutex_);
    poolResumeQueue_.push(h);
  }
  uint64_t one = 1;
  ::write(eventFd_, &one, sizeof(one));
}

void Executor::registerReadFd(int fd) { epoll_.add(fd, EPOLLIN | EPOLLET, fd); }

void Executor::enableWriteEvents(int fd) {
  auto [_, inserted] = writeInterested_.insert(fd);
  if (not inserted)
    return;
  epoll_.modify(fd, EPOLLIN | EPOLLOUT | EPOLLET, fd);
}
void Executor::disableWriteEvents(int fd) {
  auto erased = writeInterested_.erase(fd);
  if (erased == 0)
    return;
  epoll_.modify(fd, EPOLLIN | EPOLLET, fd);
}

void Executor::waitForRead(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline) {
  int seq = nextSeq_++;
  suspendedTasks_[fd] = {caller, false, seq};
  taskDeadlines_.push({deadline, fd, seq});
}

void Executor::waitForWrite(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline) {
  int seq = nextSeq_++;
  suspendedTasks_[fd] = {caller, true, seq};
  taskDeadlines_.push({deadline, fd, seq});
}

void Executor::submitFileRead(int fd, void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr,
                              uint64_t offset) {

  pendingFileOps_[nextUserData_] = {h, resultPtr};
  ioUring_.prepRead(fd, buf, len, nextUserData_, offset);
  nextUserData_++;
}

void Executor::submitFileWrite(int fd, const void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr,
                               uint64_t offset) {
  pendingFileOps_[nextUserData_] = {h, resultPtr};
  ioUring_.prepWrite(fd, buf, len, nextUserData_, offset);
  nextUserData_++;
}

void Executor::markRootFinished(void *addr) { finishedRoots_.push_back(addr); }

void Executor::run(std::atomic<bool> &shutdown) {
  tl_executor = this;
  std::chrono::steady_clock::time_point shutdownDeadline = std::chrono::steady_clock::time_point::max();
  size_t maxEvents = 512;
  epoll_event events[maxEvents];

  for (;;) {
    if (shutdown) {
      auto timeNow = now();
      if (shutdownDeadline == std::chrono::steady_clock::time_point::max()) {
        shutdownDeadline = timeNow + std::chrono::seconds(ServerConfig::GRACEFUL_SHUTDOWN_TIMEOUT_S);
      }
      if (ownedTasks_.empty()) {
        SPDLOG_INFO("Graceful Shutdown");
        return;
      }
      if (timeNow > shutdownDeadline) {
        SPDLOG_INFO("Timeout on Shutdown");
        return;
      }
    }

    ioUring_.drainCompletions([this](uint64_t userData, int result) {
      auto it = pendingFileOps_.find(userData);
      if (it == pendingFileOps_.end())
        return;

      auto [handle, resultPtr] = it->second;
      *resultPtr = result;
      readyQueue_.push({handle, false});
      pendingFileOps_.erase(it);
      return;
    });

    while (not readyQueue_.empty()) {
      auto [task, timed_out] = readyQueue_.front();
      readyQueue_.pop();
      tl_timed_out = timed_out;
      task.resume();
      tl_timed_out = false;

      while (not finishedRoots_.empty()) {
        void *addr = finishedRoots_.back();
        finishedRoots_.pop_back();

        auto it = ownedTaskMap_.find(addr);
        if (it == ownedTaskMap_.end())
          continue;

        auto index = it->second;
        auto last = ownedTasks_.size() - 1;

        if (index != last) {
          ownedTasks_[index] = std::move(ownedTasks_.back());
          ownedTaskMap_[ownedTasks_[index].handle().address()] = index;
        }
        ownedTasks_.pop_back();
        ownedTaskMap_.erase(it);
      }
    }

    // setting this directly to epoll_wait_timeout makes it so that
    // download speed = STATIC_STREAM_CHUNK_SIZE / EPOLL_WAIT_TIMEOUT seconds
    int timeout = pendingFileOps_.empty() ? ServerConfig::EPOLL_WAIT_TIMEOUT * 1000 : 0;
    int n = epoll_.wait(events, maxEvents, timeout);
    for (auto &event : std::span(events, n)) {
      int fd = event.data.fd;

      if (fd == eventFd_) {
        uint64_t val;
        ::read(eventFd_, &val, sizeof(val));
        std::unique_lock lock(poolResumeQueueMutex_);
        while (!poolResumeQueue_.empty()) {
          readyQueue_.push({poolResumeQueue_.front(), false});
          poolResumeQueue_.pop();
        }
        continue;
      }

      auto it = suspendedTasks_.find(fd);
      if (it == suspendedTasks_.end())
        continue;

      bool isError = event.events & (EPOLLERR | EPOLLHUP);
      bool readReady = event.events & EPOLLIN;
      bool writeReady = event.events & EPOLLOUT;

      bool shouldWake =
          isError || (it->second.waitingForWrite && writeReady) || (!it->second.waitingForWrite && readReady);

      if (not shouldWake)
        continue;

      readyQueue_.push({it->second.handle, false});
      suspendedTasks_.erase(it);
    }

    auto timeNow = now();
    while (not taskDeadlines_.empty() && taskDeadlines_.top().deadline <= timeNow) {
      auto [deadline, fd, seq] = taskDeadlines_.top();
      taskDeadlines_.pop();
      auto it = suspendedTasks_.find(fd);
      if (it == suspendedTasks_.end())
        continue;
      if (it->second.suspensionSeq != seq)
        continue;
      readyQueue_.push({it->second.handle, true});
      suspendedTasks_.erase(it);
    }

    ioUring_.ioSubmit();
  }
}
} // namespace rukh::core
