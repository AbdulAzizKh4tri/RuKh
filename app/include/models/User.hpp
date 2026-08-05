#pragma once

#include <nlohmann/json.hpp>

#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/DefaultThroughModel.hpp>
#include <rukh/orm/Relations.hpp>

namespace models {
using namespace rukh::orm;

struct User : ActiveRecord<User, int64_t> {

  static constexpr std::string_view tableName = "users";

  PkType id;
  std::optional<std::string> email;
  std::optional<std::string> name;
  std::optional<int64_t> age;
  std::optional<PkType> bestFriend;
  std::optional<PkType> mother;
  std::optional<std::string> password;
  int64_t createdAt;
  int64_t updatedAt;

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
                      Column{&User::bestFriend, "best_friend"},
                      Column{&User::mother, "mother"},
                      Column{&User::password, "password"},
                      Column{.fieldPtr = &User::createdAt,
                             .dbName = "created_at",
                             .autoGenerateMode = AutoGenerate::DB_NOW,
                             .autoUpdateMode = AutoUpdate::LOCKED},
                      Column{.fieldPtr = &User::updatedAt,
                             .dbName = "updated_at",
                             .autoGenerateMode = AutoGenerate::DB_NOW,
                             .autoUpdateMode = AutoUpdate::CUSTOM,
                             .customUpdator = &User::getNowTime}};
  }

  static constexpr auto relations() {
    using DTM = DefaultThroughModel<User, User, "user_friend_user">;
    constexpr auto throughField1 = ThroughField{&DTM::targetPk, &User::id, ThroughPtrType::TARGET};
    constexpr auto throughField2 = ThroughField{&DTM::definerPk, &User::id, ThroughPtrType::DEFINER};
    return std::tuple{
        manyToOne<User>(&User::bestFriend),
        manyToOne<User>(&User::mother),
        manyToManyRelation<User, User, DTM, throughField1, throughField2>().withRelationName("friendship").symmetric(),
    };
  }
};

} // namespace models
