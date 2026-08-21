/**
 * @file ThreadPool.hpp
 * @brief Awaitable fixed-size thread pool for offloading blocking and CPU-bound work
 */

#pragma once

#include <queue>

#include <rukh/Exceptions.hpp>
#include <rukh/pool/IPoolJob.hpp>
#include <rukh/pool/PoolJob.hpp>
#include <rukh/pool/PoolJobAwaitable.hpp>

namespace rukh::pool {

/// Awaitable fixed-size thread pool for offloading blocking and CPU-bound work
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

  /**
   * @brief submit a job to the threadpool that you wish to await.
   * @param callable The callable to run.
   * @returns a PoolJobAwaitable<R> that can be co_awaited, where R is the return type of the @p callable
   *
   * The job will be enqueued and eventually run, whether it is co_awaited or not.
   * It is recommended you use `fireAndForget()` if you do not care for the result or chronology.
   */
  template <typename F> [[nodiscard]] auto submit(F callable) {
    using R = std::invoke_result_t<F>;

    auto state = std::make_shared<PoolJobState<R>>();
    auto job = std::make_shared<PoolJob<F>>(std::move(callable), state);
    job->self = job;

    state->executor = core::tl_executor;

    enqueue(job.get());
    return PoolJobAwaitable<R>{std::move(state)};
  }

  /**
   * @brief Fire and Forget a job to the threadpool.
   * @param callable The callable to run.
   */
  template <typename F> void fireAndForget(F callable) {
    static_assert(noexcept(callable()), "fireAndForget() requires a noexcept callable - handle your own exceptions");

    using R = std::invoke_result_t<F>;
    using Job = PoolJob<F>;

    auto state = std::make_shared<PoolJobState<R>>();
    auto job = std::make_shared<Job>(std::move(callable), std::move(state));
    job->self = job;

    try {
      enqueue(job.get());
    } catch (ServerException &e) {
      SPDLOG_ERROR(e.what());
    }
  }

  /// Maximum number of jobs that can be queued at a time
  void setMaxQueueSize(size_t maxQueueSize) { maxQueueSize_ = maxQueueSize; }

  size_t getThreadCount() const { return workerThreads_.size(); }

private:
  std::vector<std::thread> workerThreads_;

  bool shutdown_ = false;
  size_t maxQueueSize_ = 1024;
  std::condition_variable cv_;
  std::mutex mutex_;
  std::queue<IPoolJob *> jobQueue_;

  void workerLoop() {
    for (;;) {
      IPoolJob *job = nullptr;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return not jobQueue_.empty() || shutdown_; });
        if (shutdown_ && jobQueue_.empty())
          return;
        job = jobQueue_.front();
        jobQueue_.pop();
      }
      job->runJob();
    }
  }

  void enqueue(IPoolJob *job) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (jobQueue_.size() >= maxQueueSize_)
        throw ServerException("Thread pool queue is full", 500);
      jobQueue_.push(job);
    }
    cv_.notify_one();
  }
};
} // namespace rukh::pool
