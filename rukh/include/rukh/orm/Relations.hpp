#pragma once

#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/ITransaction.hpp>

/// \todo one to one self reference integrity modes (i.e user1 -> user2 should also ensure user2 -> user1).
namespace rukh::orm {

enum class OnDelete { CASCADE, NO_ACTION, RESTRICT, SET_DEFAULT, SET_NULL };
enum class RelationType { ONE_TO_ONE, MANY_TO_ONE, MANY_TO_MANY };

//=================================================================================================================

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

  template <FixedString Name> constexpr Derived &withRelatedName() {
    relatedName = Name.view();
    return static_cast<Derived &>(*this);
  }

  constexpr Derived &noIndex() {
    index = false;
    return static_cast<Derived &>(*this);
  }

  template <FixedString Name> constexpr Derived &withConstraintName() {
    constraintName = Name.view();
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

    static_assert(std::same_as<remove_optional_t<get_field_t<decltype(fkPtr)>>, get_field_t<decltype(pkPtr)>>,
                  "FK and PK fields must be the same type. Order in composite keys is important!");

    return FieldPtrPair{fkPtr, pkPtr};
  }
};

//=================================================================================================================
template <typename TargetModel, typename DefinerModel, typename FkFieldPtrsTuple>
struct ManyToOneRelation : public FkRelation<TargetModel, DefinerModel, FkFieldPtrsTuple,
                                             ManyToOneRelation<TargetModel, DefinerModel, FkFieldPtrsTuple>> {
  static constexpr RelationType relationType = RelationType::MANY_TO_ONE;
};

template <typename TargetModel, typename DefinerModel, typename... FieldTypes>
constexpr auto manyToOne(FieldTypes DefinerModel::*...ptrs) {

  using FkFieldPtrsTuple = std::tuple<FieldTypes DefinerModel::*...>;
  using rawFkTypesTuple = std::tuple<remove_optional_t<FieldTypes>...>;
  static_assert(std::same_as<typename DefinerModel::PkTypesTuple, rawFkTypesTuple>,
                "manyToOne(): mentioned foreign keys don't match primary keys of foreign model");

  return ManyToOneRelation<TargetModel, DefinerModel, FkFieldPtrsTuple>{ptrs...};
}
//=================================================================================================================

template <typename TargetModel, typename DefinerModel, typename FkFieldPtrsTuple>
struct OneToOneRelation : public FkRelation<TargetModel, DefinerModel, FkFieldPtrsTuple,
                                            OneToOneRelation<TargetModel, DefinerModel, FkFieldPtrsTuple>> {
  static constexpr RelationType relationType = RelationType::ONE_TO_ONE;
};

template <typename TargetModel, typename DefinerModel, typename... FieldTypes>
constexpr auto oneToOne(FieldTypes DefinerModel::*...ptrs) {

  using FkFieldPtrsTuple = std::tuple<FieldTypes DefinerModel::*...>;
  using rawFkTypesTuple = std::tuple<remove_optional_t<FieldTypes>...>;
  static_assert(std::same_as<typename DefinerModel::PkTypesTuple, rawFkTypesTuple>,
                "oneToOne(): mentioned foreign keys don't match primary keys of foreign model");

  return OneToOneRelation<TargetModel, DefinerModel, FkFieldPtrsTuple>{ptrs...};
}
//=================================================================================================================

enum class ThroughPtrModel { A, B };

template <typename ThroughPtr, typename ModelPtr> struct ThroughField {
  using Model = get_class_t<ModelPtr>;

  const ThroughPtr throughPtr;
  const ModelPtr modelPtr;
  const ThroughPtrModel throughPtrModel;
};

/*
 * NONE: Defaut, non symmetric relation
 *
 * DOUBLE_ROW: Stores 2 rows per relation. Faster reads and easier to migrate to a non-symmetrical relation if needed.
 * For example, let's assume all friendships are unrealistically symmetrical.
 * If you add user1 -> friends -> user2. The ORM will add user2 -> friends -> user1 too.
 * If the model contains extra data like "since" and the row is updated, the ORM will update the mirrored row as
 * well.
 *
 * SINGLE_ROW: A single row is stored per relation. The ORM queries the mirrored relation at read time.
 * My personal Recommendation.
 *
 */
enum class SymmetryMode {
  NONE,
  SINGLE_ROW,
  DOUBLE_ROW,
  DB_SINGLE_ROW,
  DB_DOUBLE_ROW,
};

//=================================================================================================================
//========================================UGLY DefaultThroughModel Checker========================================
template <typename ModelA, typename ModelB, FixedString TableName> struct DefaultThroughModel;
template <typename T> struct IsDefaultThroughModel : std::false_type {};

template <typename A, typename B, FixedString Name>
struct IsDefaultThroughModel<DefaultThroughModel<A, B, Name>> : std::true_type {};

template <typename T>
concept DefaultThroughModelType = IsDefaultThroughModel<T>::value;
//=================================================================================================================

template <typename ModelA, typename ModelB, typename ThroughModel, ThroughField... ThroughFields>
struct ManyToManyRelation {
  using A = ModelA;
  using B = ModelB;
  using Through = ThroughModel;

  static constexpr auto throughFields = [] {
    if constexpr (sizeof...(ThroughFields) > 0) {
      static_assert(not DefaultThroughModelType<Through>, "Cannot pass ThroughFields when using DefaultThroughModel");
      return std::tuple{ThroughFields...};
    } else {
      static_assert(DefaultThroughModelType<Through>, "Must pass ThroughFields for non-default through models");
      return std::tuple{ThroughField{&Through::pkA, ModelA::pkFieldPtr(), ThroughPtrModel::A},
                        ThroughField{&Through::pkB, ModelB::pkFieldPtr(), ThroughPtrModel::B}};
    }
  }();
  using ThroughFieldTuple = decltype(throughFields);
  static constexpr size_t throughFieldCount = std::tuple_size_v<ThroughFieldTuple>;

  static constexpr bool isSelfReferential = std::is_same_v<ModelA, ModelB>;

  static_assert(throughFieldCount == 0 or throughFieldCount >= 2, "Must map fields of both models");
  static_assert(
      throughFieldCount % 2 == 0 or not isSelfReferential,
      "Self-referential relations must have an even number of through fields (2x the throughfields in the model)");

  static constexpr RelationType relationType = RelationType::MANY_TO_MANY;

  std::string_view relationName = "";
  std::string_view reciprocalName = "";
  SymmetryMode symmetryMode = SymmetryMode::NONE;

  template <FixedString Name> constexpr ManyToManyRelation &withRelationName() {
    relationName = Name.view();
    return *this;
  }

  template <FixedString Name> constexpr ManyToManyRelation &withReciprocalName() {
    static_assert(isSelfReferential, "reciprocalName only applies to self-referential relations");
    reciprocalName = Name.view();
    return *this;
  }

  template <SymmetryMode sm> constexpr ManyToManyRelation &withSymmetryMode() {
    static_assert(sm == SymmetryMode::NONE or isSelfReferential, "Symmetric relations can only be self-referencing");
    symmetryMode = sm;
    return *this;
  }

  constexpr bool isValid() const {
    const bool isSymmetric = (symmetryMode != SymmetryMode::NONE);
    if constexpr (isSelfReferential)
      return isSymmetric xor (reciprocalName == "");
    else
      return true;
  }

  static constexpr auto getThroughFieldPtrs() {
    return [&]<size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(std::tuple{std::get<I>(throughFields).throughPtr}...);
    }(std::make_index_sequence<throughFieldCount>{});
  }

  template <ThroughPtrModel T> static constexpr auto getThroughFields() {
    constexpr auto result = []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getThroughFieldIf<T, I>()...);
    }(std::make_index_sequence<std::tuple_size_v<ThroughFieldTuple>>{});
    return result;
  }

  template <ThroughPtrModel T, std::size_t I> static constexpr auto getThroughFieldIf() {
    if constexpr (std::get<I>(throughFields).throughPtrModel == T)
      return std::tuple{std::get<I>(throughFields)};
    else
      return std::tuple{};
  }

  static Through mirrorThroughObj(const Through &throughObj) {
    constexpr auto throughFieldsA = getThroughFields<ThroughPtrModel::A>();
    constexpr auto throughFieldsB = getThroughFields<ThroughPtrModel::B>();
    constexpr size_t fieldCountA = std::tuple_size_v<decltype(throughFieldsA)>;
    constexpr size_t fieldCountB = std::tuple_size_v<decltype(throughFieldsB)>;

    ThroughModel mirror = throughObj;

    [&]<size_t... I>(std::index_sequence<I...>) { // setting mirrorB = throughObjA
      (
          [&] {
            const auto aValue = throughObj.*((std::get<I>(throughFieldsA)).throughPtr);
            mirror.*((std::get<I>(throughFieldsB)).throughPtr) = aValue;
          }(),
          ...);
    }(std::make_index_sequence<fieldCountA>{});
    [&]<size_t... I>(std::index_sequence<I...>) { // setting mirrorA = throughObjB
      (
          [&] {
            const auto bValue = throughObj.*((std::get<I>(throughFieldsB)).throughPtr);
            mirror.*((std::get<I>(throughFieldsA)).throughPtr) = bValue;
          }(),
          ...);
    }(std::make_index_sequence<fieldCountB>{});
    return mirror;
  }
};

template <typename ModelA, typename ModelB, typename ThroughModel, ThroughField... ThroughFields>
constexpr auto manyToManyRelation() {
  constexpr auto result = ManyToManyRelation<ModelA, ModelB, ThroughModel, ThroughFields...>();
  static_assert(result.isValid(), "Invalid many-to-many relation, check reciprocals/symmetry mode");
  return result;
}

} // namespace rukh::orm
