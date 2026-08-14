#pragma once

#include "rukh/orm/Column.hpp"
#include <rukh/TypeHelpers.hpp>
#include <rukh/orm/ActiveRecord.hpp>
#include <rukh/orm/Constraints.hpp>

namespace rukh::orm {

template <typename ModelA, typename ModelB, FixedString TableName>
struct DefaultThroughModel : public ActiveRecord<DefaultThroughModel<ModelA, ModelB, TableName>, int64_t> {

  using PkAType = ModelA::PkType;
  using PkBType = ModelB::PkType;
  using A = ModelA;
  using B = ModelB;

  static constexpr std::string_view tableName = TableName.view();

  int64_t id;
  PkAType pkA;
  PkBType pkB;

  static constexpr auto columns() {
    return std::tuple{
        Column{.fieldPtr = &DefaultThroughModel::id,
               .dbName = "id",
               .isPrimaryKey = true,
               .autoGenerateMode = AutoGenerate::DB_INCREMENT},
        Column{.fieldPtr = &DefaultThroughModel::pkA, .dbName = "pkA"},
        Column{.fieldPtr = &DefaultThroughModel::pkB, .dbName = "pkB"},
    };
  }

  static constexpr auto relations() {
    return std::tuple{
        manyToOne<A>(&DefaultThroughModel::pkA).template onDelete<OnDelete::CASCADE>(),
        manyToOne<B>(&DefaultThroughModel::pkB).template onDelete<OnDelete::CASCADE>(),
    };
  }

  static auto constraints() {
    return std::tuple{
        makeUniqueTogether(&DefaultThroughModel::pkA, &DefaultThroughModel::pkB),
    };
  }
};

} // namespace rukh::orm
