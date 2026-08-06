#pragma once

#include <nlohmann/json.hpp>

#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/DefaultThroughModel.hpp>
#include <rukh/orm/Relations.hpp>

#include "models/PostLikeUser.hpp"
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
    using Through = PostLikeUser<User, Post>;
    static constexpr auto throughField1 = ThroughField{
        .throughPtr = &Through::postId, .modelPtr = Post::pkFieldPtr(), .throughPtrType = ThroughPtrType::DEFINER};
    static constexpr auto throughField2 = ThroughField{
        .throughPtr = &Through::userId, .modelPtr = User::pkFieldPtr(), .throughPtrType = ThroughPtrType::TARGET};

    return std::tuple{manyToOne<User>(&Post::user).onDelete<OnDelete::SET_NULL>().withRelatedName("user_posts"),
                      manyToManyRelation<User, Post, Through, throughField1, throughField2>().withRelationName(
                          "likes")};
  }
};

} // namespace models
