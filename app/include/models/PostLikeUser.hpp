#pragma once

#include <csignal>
#include <cstdint>
#include <rukh/TypeHelpers.hpp>
#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/Constraints.hpp>

#include "rukh/orm/Column.hpp"

namespace models {
using namespace rukh::orm;

template <typename TargetModel, typename DefinerModel>
struct PostLike : public ActiveRecord<PostLike<TargetModel, DefinerModel>, int64_t> {
  using Target = TargetModel;
  using Definer = DefinerModel;

  static constexpr std::string_view tableName = "post_like_user";

  int64_t id;
  int64_t userId;
  int64_t postId;

  int64_t likedAt;

  static constexpr auto columns() {
    return std::tuple{
        Column{.fieldPtr = &PostLike::id,
               .dbName = "id",
               .isPrimaryKey = true,
               .autoGenerateMode = AutoGenerate::DB_INCREMENT},
        Column{.fieldPtr = &PostLike::userId, .dbName = "user_id", .index = true},
        Column{.fieldPtr = &PostLike::postId, .dbName = "post_id", .index = true},
        Column{.fieldPtr = &PostLike::likedAt,
               .dbName = "liked_at",
               .autoGenerateMode = AutoGenerate::DB_NOW,
               .autoUpdateMode = AutoUpdate::LOCKED},
    };
  }

  static constexpr auto relations() {
    return std::tuple{
        manyToOne<Target>(&PostLike::userId).template onDelete<OnDelete::CASCADE>().withRelatedName("user_likes"),
        manyToOne<Definer>(&PostLike::postId).template onDelete<OnDelete::CASCADE>().withRelatedName("post_likes"),
    };
  }

  static auto constraints() {
    return std::tuple{
        makeUniqueTogether(&PostLike::userId, &PostLike::postId),
    };
  }
};

} // namespace models
