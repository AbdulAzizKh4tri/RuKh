#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>
#include <rukh/orm/Column.hpp>

namespace models {
using namespace rukh::orm;

struct User {
  using idType = int64_t;

  static constexpr std::string modelName = "User";

  idType id;
  int64_t age;
  std::string name;
  std::string email;
  std::string password;

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
