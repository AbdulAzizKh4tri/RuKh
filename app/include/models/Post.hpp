#pragma once

#include <nlohmann/json.hpp>

#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/DefaultThroughModel.hpp>
#include <rukh/orm/Relations.hpp>

#include "models/User.hpp"

namespace models {
using namespace rukh::orm;

struct Post : ActiveRecord<Post, int64_t> {

  static constexpr std::string_view tableName = "posts";

  PkType id;
  std::string title;
  std::string content;
  std::optional<User::PkType> user;
  int64_t createdAt;
  int64_t updatedAt;

  rukh::db::DbValue getNowTime() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  static constexpr auto columns() {
    return std::tuple{Column{.fieldPtr = &Post::id,
                             .dbName = "id",
                             .isPrimaryKey = true,
                             .autoGenerateMode = AutoGenerate::DB_INCREMENT},

                      Column{&Post::title, "title"},
                      Column{&Post::content, "content"},
                      Column{&Post::user, "user_id"},

                      Column{.fieldPtr = &Post::createdAt,
                             .dbName = "created_at",
                             .autoGenerateMode = AutoGenerate::DB_NOW,
                             .autoUpdateMode = AutoUpdate::LOCKED},

                      Column{.fieldPtr = &Post::updatedAt,
                             .dbName = "updated_at",
                             .autoGenerateMode = AutoGenerate::DB_NOW,
                             .autoUpdateMode = AutoUpdate::CUSTOM,
                             .customUpdator = &Post::getNowTime}};
  }

  static constexpr auto relations() {
    using DTM = DefaultThroughModel<User, Post, "post_like_user">;

    return std::tuple{manyToOne<User>(&Post::user).onDelete<OnDelete::SET_NULL>().withRelatedName("user_posts"),
                      manyToManyRelation<User, Post, DTM>().withRelationName("post_liked_user_very_long_string_for_testing_purposes")};
  }
};

} // namespace models
