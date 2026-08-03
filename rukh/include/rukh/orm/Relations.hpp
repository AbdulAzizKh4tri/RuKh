#pragma once

#include <concepts>
#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbTypes.hpp>

namespace rukh::orm {

// TODO: Add custom deleters
enum class OnDelete { CASCADE, NO_ACTION, RESTRICT, SET_DEFAULT, SET_NULL, CUSTOM };

template <typename TargetModel, typename DefinerModel, typename FkFieldPtrsTuple, typename Derived> struct FkRelation {
  using PkFieldPtrs = decltype(TargetModel::pkFieldPtrs());
  using Target = TargetModel;   // For ActiveRecord to use
  using Definer = DefinerModel; //
  using customDeleterType = bool (TargetModel::*)() const;

  FkFieldPtrsTuple fkFieldPtrs;
  PkFieldPtrs pkFieldPtrs = TargetModel::pkFieldPtrs();

  OnDelete onDeletePolicy = OnDelete::NO_ACTION;
  customDeleterType customDeleter = nullptr;

  std::string_view relatedName = "";

  bool index = true;
  std::string_view constraintName = "";

  template <OnDelete od, customDeleterType deleter = nullptr> constexpr Derived &onDelete() {
    if constexpr (od == OnDelete::SET_NULL) {
      static_assert(AllOptionalFieldPtrs<FkFieldPtrsTuple>, "FK fields must be optional<> for OnDelete::SET_NULL");
    }
    if constexpr (od == OnDelete::CUSTOM) {
      static_assert(deleter != nullptr, "OnDelete::CUSTOM requires a non-null deleter");
    } else {
      static_assert(deleter == nullptr, "deleter only meaningful for OnDelete::CUSTOM");
    }
    onDeletePolicy = od;
    customDeleter = deleter;
    return static_cast<Derived &>(*this);
  }

  constexpr Derived &withRelatedName(const std::string_view name) {
    relatedName = name;
    return static_cast<Derived &>(*this);
  }

  constexpr Derived &noIndex() {
    index = false;
    return static_cast<Derived &>(*this);
  }

  constexpr Derived &withConstraintName(const std::string_view name) {
    constraintName = name;
    return static_cast<Derived &>(*this);
  }

  constexpr auto getFieldPtrPairs() const {
    auto result = [&]<std::size_t... I>(std::index_sequence<I...>) {
      return std::make_tuple(getFieldPtrPair<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<decltype(pkFieldPtrs)>>>{});
    return result;
  }

  template <std::size_t I> constexpr auto getFieldPtrPair() const {
    auto pkPtr = std::get<I>(pkFieldPtrs);
    auto fkPtr = std::get<I>(fkFieldPtrs);

    static_assert(std::same_as<remove_optional_t<remove_member_pointer_t<decltype(fkPtr)>>,
                               remove_member_pointer_t<decltype(pkPtr)>>,
                  "FK and PK fields must be the same type. Order is important!");

    return std::pair{fkPtr, pkPtr};
  }
};

template <typename TargetModel, typename DefinerModel, typename FkFieldPtrsTuple>
struct ManyToOneRelation : public FkRelation<TargetModel, DefinerModel, FkFieldPtrsTuple,
                                             ManyToOneRelation<TargetModel, DefinerModel, FkFieldPtrsTuple>> {};

template <typename TargetModel, typename DefinerModel, typename... FieldTypes>
constexpr auto manyToOne(FieldTypes DefinerModel::*...ptrs) {

  using FkFieldPtrsTuple = std::tuple<FieldTypes DefinerModel::*...>;
  using rawFkTypesTuple = std::tuple<remove_optional_t<FieldTypes>...>;
  static_assert(std::same_as<typename DefinerModel::PkTypesTuple, rawFkTypesTuple>,
                "manyToOne(): mentioned foreign keys don't match primary keys of foreign model");

  return ManyToOneRelation<TargetModel, DefinerModel, FkFieldPtrsTuple>{ptrs...};
}

template <typename TargetModel, typename DefinerModel, typename FkFieldPtrsTuple>
struct OneToOneRelation : public FkRelation<TargetModel, DefinerModel, FkFieldPtrsTuple,
                                            OneToOneRelation<TargetModel, DefinerModel, FkFieldPtrsTuple>> {};

template <typename TargetModel, typename DefinerModel, typename... FieldTypes>
constexpr auto oneToOne(FieldTypes DefinerModel::*...ptrs) {

  using FkFieldPtrsTuple = std::tuple<FieldTypes DefinerModel::*...>;
  using rawFkTypesTuple = std::tuple<remove_optional_t<FieldTypes>...>;
  static_assert(std::same_as<typename DefinerModel::PkTypesTuple, rawFkTypesTuple>,
                "oneToOne(): mentioned foreign keys don't match primary keys of foreign model");

  return OneToOneRelation<TargetModel, DefinerModel, FkFieldPtrsTuple>{ptrs...};
}

} // namespace rukh::orm
