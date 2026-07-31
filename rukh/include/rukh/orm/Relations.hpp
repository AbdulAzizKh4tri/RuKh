#pragma once

#include <rukh/db/DbTypes.hpp>

namespace rukh::orm {

template <typename OneModel, typename ManyModel, typename FkFieldPtrsTuple> struct ManyToOneRelation {
  using PkFieldPtrs = decltype(OneModel::pkFieldPtrs());

  FkFieldPtrsTuple fkFieldPtrs;
  PkFieldPtrs pkFieldPtrs = OneModel::pkFieldPtrs();
};

template <typename OneModel, typename ManyModel, typename... FieldTypes>
constexpr auto manyToOne(FieldTypes ManyModel::*...ptrs) {
  return ManyToOneRelation<OneModel, ManyModel, std::tuple<FieldTypes ManyModel::*...>>{ptrs...};
}

template <typename ModelA, typename ModelB, typename FkFieldPtrsTuple> struct OneToOneRelation {
  using PkFieldPtrs = decltype(ModelB::pkFieldPtrs());

  FkFieldPtrsTuple fkFieldPtrs;
  PkFieldPtrs pkFieldPtrs = ModelB::pkFieldPtrs();
};

template <typename ModelA, typename ModelB, typename... FieldTypes>
constexpr auto oneToOne(FieldTypes ModelB::*...ptrs) {
  return OneToOneRelation<ModelA, ModelB, std::tuple<FieldTypes ModelB::*...>>{ptrs...};
}

} // namespace rukh::orm
