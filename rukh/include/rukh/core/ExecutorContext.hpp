/**
 * @file ExecutorContext.hpp
 * @brief Thread local Executor context, and helper function.
 */

#pragma once

#include <coroutine>

namespace rukh::core {

class Executor;

extern thread_local Executor *tl_executor;
extern thread_local bool tl_timed_out;

void notifyTaskFinished(std::coroutine_handle<> h) noexcept;

} // namespace rukh::core
