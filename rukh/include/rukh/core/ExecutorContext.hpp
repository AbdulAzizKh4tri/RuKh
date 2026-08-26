/**
 * @file ExecutorContext.hpp
 * @brief Thread local Executor context, and helper function.
 */

#pragma once

#include <coroutine>

#include <rukh/core/FileCache.hpp>
#include <rukh/core/FileIoHelpers.hpp>

namespace rukh::core {

class Executor;

extern thread_local Executor *tl_executor;
extern thread_local bool tl_timed_out;
extern thread_local PipePool tl_pipe_pool;
extern thread_local FileCache tl_file_cache;

void notifyTaskFinished(std::coroutine_handle<> h) noexcept;

} // namespace rukh::core
