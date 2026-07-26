#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <rukh/ThreadPool.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/Column.hpp>

namespace models {
using namespace rukh::orm;

struct User : ActiveRecord<User, int64_t> {

  static constexpr std::string tableName = "users";
  static constexpr std::string modelName = "User";
  static constexpr bool pkAutoIncrement = true;
  static pk getNextPk() { return 1; }

  inline static rukh::db::IDatabase *db = nullptr;
  inline static rukh::ThreadPool *threadPool = nullptr;

  pk id;
  int64_t age;
  std::string name;
  std::string email;
  std::string password;

  static constexpr rukh::orm::Column<pk, User> pkColumn() { return rukh::orm::Column<pk, User>{"id", &User::id}; }

  static constexpr auto columns() {
    return std::tuple{Column{"id", &User::id}, Column{"age", &User::age}, Column{"name", &User::name},
                      Column{"email", &User::email}, Column{"password", &User::password}};
  }

  std::string toString() const {
    return std::to_string(id) + ": " + name + " " + email + " " + std::to_string(age) + " " + password;
  }

  nlohmann::json toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["email"] = email;
    j["age"] = age;
    j["password"] = password;
    return j;
  }
};

} // namespace models
