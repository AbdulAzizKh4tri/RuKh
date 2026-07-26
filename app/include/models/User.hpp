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
  std::optional<std::string> name;
  std::optional<std::string> email;
  std::optional<int64_t> age;
  std::optional<std::string> password;
  int64_t createdAt;

  static constexpr rukh::orm::Column<pk, User> pkColumn() { return rukh::orm::Column<pk, User>{"id", &User::id}; }

  static constexpr auto columns() {
    return std::tuple{
        Column{"id", &User::id},       Column{"age", &User::age},           Column{"name", &User::name},
        Column{"email", &User::email}, Column{"password", &User::password}, Column{"created_at", &User::createdAt}};
  }

  template <auto FieldPtr> static constexpr std::string columnNameOf() {
    std::string result;
    std::apply([&](auto &&...col) { ((col.fieldPtr == FieldPtr ? (result = col.name, void()) : void()), ...); },
               columns());
    return result;
  }

  std::string toString() const {
    return std::to_string(id) + ": " + name.value_or("null") + " " + email.value_or("null") + " " +
           std::to_string(age.value_or(0)) + " " + password.value_or("null");
  }

  nlohmann::json toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name.value_or("null");
    j["email"] = email.value_or("null");
    j["age"] = age.value_or(0);
    j["password"] = password.value_or("null");

    return j;
  }
};

} // namespace models
