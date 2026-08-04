#pragma once

#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/ITransaction.hpp>

// TODO: one to one self reference integrity modes
namespace rukh::orm {

enum class OnDelete { CASCADE, NO_ACTION, RESTRICT, SET_DEFAULT, SET_NULL };

template <typename TargetModel, typename DefinerModel, typename FkFieldPtrsTuple, typename Derived> struct FkRelation {
  using PkFieldPtrs = decltype(TargetModel::pkFieldPtrs());
  using Target = TargetModel;   // For ActiveRecord to use
  using Definer = DefinerModel; //

  FkFieldPtrsTuple fkFieldPtrs;
  PkFieldPtrs pkFieldPtrs = TargetModel::pkFieldPtrs();

  OnDelete onDeletePolicy = OnDelete::NO_ACTION;

  std::string_view relatedName = "";

  bool index = true;
  std::string_view constraintName = "";

  template <OnDelete od> constexpr Derived &onDelete() {
    if constexpr (od == OnDelete::SET_NULL) {
      static_assert(AllOptionalFieldPtrs<FkFieldPtrsTuple>, "FK fields must be optional<> for OnDelete::SET_NULL");
    }
    onDeletePolicy = od;
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

  template <typename FkPtr, typename PkPtr> struct FieldPtrPair {
    FkPtr fkPtr;
    PkPtr pkPtr;
  };

  constexpr auto getFieldPtrPairs() const {
    auto result = [&]<std::size_t... I>(std::index_sequence<I...>) {
      return std::make_tuple(getFieldPtrPair<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<decltype(pkFieldPtrs)>>>{});
    return result;
  }

  template <std::size_t I> constexpr auto getFieldPtrPair() const {
    auto pkPtr = std::get<I>(pkFieldPtrs);
    auto fkPtr = std::get<I>(fkFieldPtrs);

    static_assert(std::same_as<remove_optional_t<get_field_t<decltype(fkPtr)>>,
                               get_field_t<decltype(pkPtr)>>,
                  "FK and PK fields must be the same type. Order in composite keys is important!");

    return FieldPtrPair{fkPtr, pkPtr};
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

template <typename T> struct is_one_to_one_relation : std::false_type {};
template <typename ModelA, typename ModelB, typename FkFieldPtrsTuple>
struct is_one_to_one_relation<OneToOneRelation<ModelA, ModelB, FkFieldPtrsTuple>> : std::true_type {};
template <typename T> static constexpr bool is_one_to_one_relation_v = is_one_to_one_relation<T>::value;

template <typename T> struct is_many_to_one_relation : std::false_type {};
template <typename ModelA, typename ModelB, typename FkFieldPtrsTuple>
struct is_many_to_one_relation<ManyToOneRelation<ModelA, ModelB, FkFieldPtrsTuple>> : std::true_type {};
template <typename T> static constexpr bool is_many_to_one_relation_v = is_many_to_one_relation<T>::value;
} // namespace rukh::orm
