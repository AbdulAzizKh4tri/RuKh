#pragma once

#include <queue>

#include <rukh/Exceptions.hpp>
#include <rukh/core/IPoolTask.hpp>
#include <rukh/core/PoolTask.hpp>
#include <rukh/core/PoolTaskAwaitable.hpp>

namespace rukh {

class ThreadPool {
public:
  ThreadPool(size_t poolSize) {
    for (size_t i = 0; i < poolSize; i++) {
      workerThreads_.emplace_back([this] { workerLoop(); });
    }
  };

  ~ThreadPool() {
    {
      std::unique_lock lock(mutex_);
      shutdown_ = true;
    }
    cv_.notify_all();
    for (auto &t : workerThreads_)
      t.join();
  }

  void enqueue(IPoolTask *task) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (taskQueue_.size() >= maxQueueSize_)
        throw ServerException("Thread pool queue is full", 500);
      taskQueue_.push(task);
    }
    cv_.notify_one();
  }

  template <typename F> [[nodiscard]] auto submit(F callable) {
    using R = std::invoke_result_t<F>;

    auto state = std::make_shared<TaskState<R>>();
    auto task = std::make_shared<PoolTask<F, R>>(std::move(callable), state);
    task->self = task;

    state->executor = core::tl_executor;

    enqueue(task.get());
    return PoolTaskAwaitable<R>{std::move(state)};
  }

  template <typename F> void fireAndForget(F callable) {
    static_assert(noexcept(callable()), "fireAndForget() requires a noexcept callable - handle your own exceptions");

    using R = std::invoke_result_t<F>;
    using Task = PoolTask<F, R>;

    auto state = std::make_shared<TaskState<R>>();
    auto task = std::make_shared<Task>(std::move(callable), std::move(state));
    task->self = task;

    try {
      enqueue(task.get());
    } catch (ServerException &e) {
      SPDLOG_ERROR(e.what());
    }
  }

  void setMaxQueueSize(size_t maxQueueSize) { maxQueueSize_ = maxQueueSize; }

  size_t getThreadCount() const { return workerThreads_.size(); }

private:
  std::vector<std::thread> workerThreads_;

  bool shutdown_ = false;
  size_t maxQueueSize_ = 1024;
  std::condition_variable cv_;
  std::mutex mutex_;
  std::queue<IPoolTask *> taskQueue_;

  void workerLoop() {
    for (;;) {
      IPoolTask *task = nullptr;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return !taskQueue_.empty() || shutdown_; });
        if (shutdown_ && taskQueue_.empty())
          return;
        task = taskQueue_.front();
        taskQueue_.pop();
      }
      task->runTask();
    }
  }
};
} // namespace rukh
