#pragma once

#include <rukh/TypeHelpers.hpp>
#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/Constraints.hpp>

namespace rukh::orm {

template <typename TargetModel, typename DefinerModel, FixedString TableName>
struct DefaultThroughModel : public ActiveRecord<DefaultThroughModel<TargetModel, DefinerModel, TableName>, int64_t> {

  using TargetPkType = TargetModel::PkType;
  using DefinerPkType = DefinerModel::PkType;
  using Target = TargetModel;
  using Definer = DefinerModel;

  static constexpr std::string_view tableName = TableName.view();

  int64_t id;
  TargetPkType targetPk;
  DefinerPkType definerPk;

  static constexpr auto columns() {
    return std::tuple{
        Column{.fieldPtr = &DefaultThroughModel::id, .dbName = "id", .isPrimaryKey = true},
        Column{.fieldPtr = &DefaultThroughModel::targetPk, .dbName = "pkA", .index = true},
        Column{.fieldPtr = &DefaultThroughModel::definerPk, .dbName = "pkB", .index = true},
    };
  }

  static constexpr auto relations() {
    return std::tuple{
        manyToOne<Target>(&DefaultThroughModel::targetPk),
        manyToOne<Definer>(&DefaultThroughModel::definerPk),
    };
  }

  static auto constraints() {
    return std::tuple{
        makeUniqueTogether(&DefaultThroughModel::targetPk, &DefaultThroughModel::definerPk),
    };
  }

};

} // namespace rukh::orm
