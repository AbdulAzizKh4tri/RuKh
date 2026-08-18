#pragma once

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include <rukh/core/EpollInstance.hpp>
#include <rukh/core/ExecutorContext.hpp>
#include <rukh/core/IoUringInstance.hpp>
#include <rukh/core/Task.hpp>

namespace rukh {

class Executor {
  struct SuspendedTask {
    std::coroutine_handle<> handle;
    bool waitingForWrite;
    int suspensionSeq;
  };

  struct TaskDeadline {
    std::chrono::steady_clock::time_point deadline;
    int fd;
    int suspensionSeq;

    bool operator>(const TaskDeadline &other) const { return deadline > other.deadline; }
  };

  struct ReadyTask {
    std::coroutine_handle<> task;
    bool timedOut;
  };

public:
  Executor();
  ~Executor() = default;

  void spawn(core::Task<void> task);

  void unregister(int fd);

  void registerReadFd(int fd);
  void enableWriteEvents(int fd);
  void disableWriteEvents(int fd);

  void waitForRead(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline);

  void waitForWrite(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline);

  void submitFileRead(int fd, void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr, uint64_t offset);

  void submitFileWrite(int fd, const void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr, uint64_t offset);

  void run(std::atomic<bool> &shutdown);

  void post(std::coroutine_handle<> h);

  void markRootFinished(void *addr);

private:
  EpollInstance epoll_;
  std::vector<core::Task<void>> ownedTasks_;
  std::unordered_map<void *, size_t> ownedTaskMap_;
  std::queue<ReadyTask> readyQueue_;

  std::vector<void *> finishedRoots_;

  int nextSeq_ = 0;
  std::unordered_map<int, SuspendedTask> suspendedTasks_;
  std::priority_queue<TaskDeadline, std::vector<TaskDeadline>, std::greater<TaskDeadline>> taskDeadlines_;

  std::unordered_set<int> writeInterested_;

  IoUringInstance ioUring_;
  uint64_t nextUserData_ = 0;
  std::unordered_map<uint64_t, std::pair<std::coroutine_handle<>, int *>> pendingFileOps_;

  int eventFd_;
  std::mutex poolResumeQueueMutex_;
  std::queue<std::coroutine_handle<>> poolResumeQueue_;
};
} // namespace rukh
