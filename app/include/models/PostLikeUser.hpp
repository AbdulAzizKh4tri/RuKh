#pragma once

#include <csignal>
#include <cstdint>
#include <rukh/TypeHelpers.hpp>
#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/Constraints.hpp>

#include "rukh/orm/Column.hpp"

namespace rukh::orm {

template <typename TargetModel, typename DefinerModel>
struct PostLikeUser : public ActiveRecord<PostLikeUser<TargetModel, DefinerModel>, int64_t> {
  using Target = TargetModel;
  using Definer = DefinerModel;

  static constexpr std::string_view tableName = "post_like_user";

  int64_t id;
  int64_t userId;
  int64_t postId;

  int64_t likedAt;

  static constexpr auto columns() {
    return std::tuple{
        Column{.fieldPtr = &PostLikeUser::id, .dbName = "id", .isPrimaryKey = true},
        Column{.fieldPtr = &PostLikeUser::userId, .dbName = "user_id", .index = true},
        Column{.fieldPtr = &PostLikeUser::postId, .dbName = "post_id", .index = true},
        Column{.fieldPtr = &PostLikeUser::likedAt,
               .dbName = "liked_at",
               .autoGenerateMode = AutoGenerate::DB_NOW,
               .autoUpdateMode = AutoUpdate::LOCKED},
    };
  }

  static constexpr auto relations() {
    return std::tuple{
        manyToOne<Target>(&PostLikeUser::userId).template onDelete<OnDelete::CASCADE>().withRelatedName("user_likes"),
        manyToOne<Definer>(&PostLikeUser::postId).template onDelete<OnDelete::CASCADE>().withRelatedName("post_likes"),
    };
  }

  static auto constraints() {
    return std::tuple{
        makeUniqueTogether(&PostLikeUser::userId, &PostLikeUser::postId),
    };
  }
};

} // namespace rukh::orm
