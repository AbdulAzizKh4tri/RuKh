#pragma once

#include <rukh/ThreadPool.hpp>
#include <rukh/db/IDatabase.hpp>

namespace rukh::orm {

struct OrmConfig {
  inline static rukh::db::IDatabase *db = nullptr;
};
} // namespace rukh::orm
