#pragma once

#include <concepts>
#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbTypes.hpp>

namespace rukh::orm {

enum class OnDelete { CASCADE, NO_ACTION, RESTRICT, SET_DEFAULT, SET_NULL, CUSTOM };

template <typename TargetModel, typename FieldModel, typename FkFieldPtrsTuple, typename Derived> struct FkRelation {
  using PkFieldPtrs = decltype(TargetModel::pkFieldPtrs());
  FkFieldPtrsTuple fkFieldPtrs;
  PkFieldPtrs pkFieldPtrs = TargetModel::pkFieldPtrs();

  // TODO: Add custom deleters
  OnDelete onDelete = OnDelete::NO_ACTION;

  template <OnDelete od> Derived &with() {
    if constexpr (od == OnDelete::SET_NULL) {
      static_assert(AllOptionalFieldPtrs<FkFieldPtrsTuple>, "FK fields must be optional<> for OnDelete::Set_NULL");
    }
    onDelete = od;
    return static_cast<Derived &>(*this);
  }
};

template <typename TargetModel, typename FieldModel, typename FkFieldPtrsTuple>
struct ManyToOneRelation : public FkRelation<TargetModel, FieldModel, FkFieldPtrsTuple,
                                             ManyToOneRelation<TargetModel, FieldModel, FkFieldPtrsTuple>> {};

template <typename TargetModel, typename FieldModel, typename... FieldTypes>
constexpr auto manyToOne(FieldTypes FieldModel::*...ptrs) {

  using FkFieldPtrsTuple = std::tuple<FieldTypes FieldModel::*...>;
  using rawFkTypesTuple = std::tuple<remove_optional_t<FieldTypes>...>;
  static_assert(std::same_as<typename FieldModel::PkTypesTuple, rawFkTypesTuple>,
                "manyToOne(): mentioned foreign keys don't match primary keys of foreign model");

  return ManyToOneRelation<TargetModel, FieldModel, FkFieldPtrsTuple>{ptrs...};
}

template <typename TargetModel, typename FieldModel, typename FkFieldPtrsTuple>
struct OneToOneRelation : public FkRelation<TargetModel, FieldModel, FkFieldPtrsTuple,
                                            OneToOneRelation<TargetModel, FieldModel, FkFieldPtrsTuple>> {};

template <typename TargetModel, typename FieldModel, typename... FieldTypes>
constexpr auto oneToOne(FieldTypes FieldModel::*...ptrs) {

  using FkFieldPtrsTuple = std::tuple<FieldTypes FieldModel::*...>;
  using rawFkTypesTuple = std::tuple<remove_optional_t<FieldTypes>...>;
  static_assert(std::same_as<typename FieldModel::PkTypesTuple, rawFkTypesTuple>,
                "oneToOne(): mentioned foreign keys don't match primary keys of foreign model");

  return OneToOneRelation<TargetModel, FieldModel, FkFieldPtrsTuple>{ptrs...};
}

} // namespace rukh::orm
