#pragma once

#include <rukh/TypeHelpers.hpp>
#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/Column.hpp>
#include <rukh/orm/Constraints.hpp>

namespace models {
using namespace rukh::orm;

template <typename UserTmp> struct Follow : public ActiveRecord<Follow<UserTmp>, int64_t> {
  using U = UserTmp;
  static constexpr std::string_view tableName = "user_follows_user";

  int64_t id;
  int64_t followee;
  int64_t follower;

  std::optional<int64_t> randomNum;
  std::optional<std::string> str;

  static constexpr auto columns() {
    return std::tuple{
        Column{.fieldPtr = &Follow::id,
               .dbName = "id",
               .isPrimaryKey = true,
               .autoGenerateMode = AutoGenerate::DB_INCREMENT},
        Column{.fieldPtr = &Follow::followee, .dbName = "followee_id"},
        Column{.fieldPtr = &Follow::follower, .dbName = "follower_id"},
        Column{.fieldPtr = &Follow::randomNum, .dbName = "random_num"},
        Column{.fieldPtr = &Follow::str, .dbName = "str"},
    };
  }

  static constexpr auto relations() {
    return std::tuple{
        manyToOne<U>(&Follow::followee)
            .template onDelete<OnDelete::CASCADE>()
            .template withRelatedName<"user_followeds">(),
        manyToOne<U>(&Follow::follower)
            .template onDelete<OnDelete::CASCADE>()
            .template withRelatedName<"user_follows">(),
    };
  }

  static auto constraints() {
    return std::tuple{
        makeUniqueTogether(&Follow::followee, &Follow::follower),
    };
  }
};

} // namespace models
