/**
 * @file IPoolJob.hpp
 * @brief Interface for PoolJob type erasure
 */

#pragma once

namespace rukh::pool {

/// Interface for PoolJob type erasure
struct IPoolJob {
  virtual void runJob() = 0;
  virtual ~IPoolJob() = default;
};
} // namespace rukh::pool
