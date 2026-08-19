#pragma once

#include <rukh/db/IDatabase.hpp>
#include <rukh/pool/ThreadPool.hpp>

namespace rukh::orm {

struct OrmConfig {
  inline static rukh::db::IDatabase *db = nullptr;
};
} // namespace rukh::orm
