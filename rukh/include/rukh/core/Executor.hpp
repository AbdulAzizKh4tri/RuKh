/**
 * @file Executor.hpp
 * @brief The RuKh Executor, used to run coroutines. Everything passes through this.
 */

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

namespace rukh::core {

/// The RuKh Executor, used to manage coroutines. Everything passes through this.
class Executor {
public:
  /// A struct to represent a suspended Task
  struct SuspendedTask {
    /// The actual coroutine handle to resume when the I/O for this Task is ready
    std::coroutine_handle<> handle;
    bool waitingForWrite;
    /// Used as an ID of sorts to compare with @c TaskDeadline
    int suspensionSeq;
  };

  /// A struct to set deadlines for tasks
  struct TaskDeadline {
    /// The deadline for this task
    std::chrono::steady_clock::time_point deadline;
    int fd;
    /// Used as an ID of sorts to compare with @c SuspendedTask
    int suspensionSeq;

    bool operator>(const TaskDeadline &other) const { return deadline > other.deadline; }
  };

  /// A struct to represent a ready Task
  struct ReadyTask {
    std::coroutine_handle<> task;
    /// Used to let the thread know the current task is timed out. Used by the server and I/O classes.
    bool timedOut;
  };

  Executor();
  ~Executor() = default;

  /**
   * @brief Spawn a new @c Task
   *
   * Moves the @c Task into the Executor, owning it, and pushes the handle into the ready queue to be resumed.
   * This is the only way other than co_await to run a @c Task.
   */
  void spawn(core::Task<void> task);

  void unregister(int fd);

  void registerReadFd(int fd);
  void enableWriteEvents(int fd);
  void disableWriteEvents(int fd);

  /// Wait on a file fd for READ until deadline
  void waitForRead(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline);

  /// Wait on a file fd for WRITE until deadline
  void waitForWrite(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline);

  /**
   * @brief Submit a file read operation
   *
   * @param fd the file fd to read from.
   * @param buf the buffer to write the read data into
   * @param len the length of data to be read
   * @param h the coroutine handle to resume once read completes
   * @param resultPtr pointer to the int that stores the number of bytes read. @c FileReadAwaitable
   * @param offset The offset at which to read the file from, -1 to let the system keep track of that.
   *
   * @see FileReadAwaitable
   *
   */
  void submitFileRead(int fd, void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr, uint64_t offset);

  /**
   * @brief Submit a file write operation
   *
   * @param fd the file fd to write to
   * @param buf the buffer to write the data from.
   * @param len the length of data to be written
   * @param h the coroutine handle to resume once the write completes
   * @param resultPtr pointer to the int that stores the number of bytes written. @c FileWriteAwaitable
   * @param offset The offset at which to start writing buf to the file, -1 to let the system keep track of that.
   *
   * @see FileWriteAwaitable
   */
  void submitFileWrite(int fd, const void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr, uint64_t offset);

  /**
   * @brief The executor loop that runs everything.
   *
   * @verbatim
    simplified loop:
    {
     check shutdown flag. If set, try to shut down gracefully, then terminate after timeout or interrupt from user.
     check ioUring for completions and push the awaiters to the ready queue.
     resume ready tasks.
     wait for epoll events. Handle resuming of Tasks waiting for PoolTasks or I/O.
     sweep the suspendedTasks for deadlines.
     submit any new file I/O using @c IoUringInstance  's @c ioSubmit
    }
    @endverbatim
   *
   */
  void run(std::atomic<bool> &shutdown);

  /// Used by @c ThreadPool to post tasks to the @c Executor
  void post(std::coroutine_handle<> h);

  /// Mark a Task started using @c spawn as finished.
  void markRootFinished(void *addr);

private:
  core::EpollInstance epoll_;
  std::vector<core::Task<void>> ownedTasks_;
  std::unordered_map<void *, size_t> ownedTaskMap_;
  std::queue<ReadyTask> readyQueue_;

  std::vector<void *> finishedRoots_;

  int nextSeq_ = 0;
  std::unordered_map<int, SuspendedTask> suspendedTasks_;
  std::priority_queue<TaskDeadline, std::vector<TaskDeadline>, std::greater<TaskDeadline>> taskDeadlines_;

  /// Keeps track of FDs so we don't waste syscalls.
  std::unordered_set<int> writeInterested_;

  IoUringInstance ioUring_;
  uint64_t nextUserData_ = 0; // unique ID for IO uring file ops
  std::unordered_map<uint64_t, std::pair<std::coroutine_handle<>, int *>> pendingFileOps_;

  int eventFd_;
  std::mutex poolResumeQueueMutex_;
  std::queue<std::coroutine_handle<>> poolResumeQueue_;
};
} // namespace rukh::core
