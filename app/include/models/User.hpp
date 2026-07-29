#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <rukh/ThreadPool.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/Column.hpp>

namespace models {
using namespace rukh::orm;

struct User : ActiveRecord<User, int64_t> {

  static constexpr std::string tableName = "users";
  static constexpr std::string modelName = "User";

  inline static rukh::db::IDatabase *db = nullptr;
  inline static rukh::ThreadPool *threadPool = nullptr;

  int64_t id;
  std::optional<std::string> email;
  std::optional<std::string> name;
  std::optional<int64_t> age;
  std::optional<std::string> password;
  int64_t createdAt;

  rukh::db::DbValue getNowTime() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  static constexpr auto columns() {
    return std::tuple{Column{.fieldPtr = &User::id,
                             .dbName = "id",
                             .isPrimaryKey = true,
                             .autoGenerateMode = AutoGenerate::DB_INCREMENT},
                      Column{&User::email, "email"},
                      Column{&User::name, "name"},
                      Column{&User::age, "age"},
                      Column{&User::password, "password"},
                      Column{.fieldPtr = &User::createdAt, .dbName = "created_at"}};
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
