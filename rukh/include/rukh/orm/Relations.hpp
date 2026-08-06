#pragma once

#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/ITransaction.hpp>

// TODO: one to one self reference integrity modes (i.e user1 -> user2 should also ensure user2 -> user1).
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

enum class ThroughPtrType { TARGET, DEFINER };

template <typename ThroughPtr, typename ModelPtr> struct ThroughField {
  using Model = get_class_t<ModelPtr>;

  const ThroughPtr throughPtr;
  const ModelPtr modelPtr;
  const ThroughPtrType throughPtrType;
};

/*
 * NONE: Defaut, non symmetric relation
 *
 * DB_WRITE: The Database runs a trigger after each insert/update that inserts/updates the mirrored relation.
 * for example, let's assume all friendships are symmetrical.
 * user1 -> friends -> user2. The database will run a trigger to add user2 -> friends user1.
 * If the model contains extra data like "since", the DB trigger will update the mirrored row on updation as well.
 * Stores 2 rows per relation, easier to migrate to a non-symmetrical relation if needed.
 *
 * READ: The ORM queries the mirrored relation as well.
 *
 */
enum class SymmetryMode { NONE, DB_WRITE, READ };

template <typename TargetModel, typename DefinerModel, typename ThroughModel, ThroughField... ThroughFields>
struct ManyToManyRelation {
  using Target = TargetModel;
  using Definer = DefinerModel;
  using Through = ThroughModel;

  static constexpr auto throughFields = std::tuple{ThroughFields...};
  static constexpr size_t throughFieldCount = sizeof...(ThroughFields);
  static_assert(throughFieldCount == 0 or throughFieldCount >= 2, "Must map fields of both models");
  static constexpr RelationType relationType = RelationType::MANY_TO_MANY;

  std::string_view relationName = "";
  SymmetryMode symmetryMode = SymmetryMode::NONE;

  // Not needed if only one manyToMany relation exists between two models.
  constexpr ManyToManyRelation &withRelationName(const std::string_view name) {
    relationName = name;
    return *this;
  }

  template <SymmetryMode sm> constexpr ManyToManyRelation &withSymmetryMode() {
    static_assert((sm != SymmetryMode::NONE and throughFieldCount % 2 == 0 and std::is_same_v<Target, Definer>) or
                      (sm == SymmetryMode::NONE),
                  "Symmetric relations can only be self-referencing (Let me know if I'm wrong).");
    symmetryMode = sm;
    return *this;
  }
};

/*
 * TargetModel: The target of this relationship. Generally to be thought of as the parent model.
 *
 * DefinerModel: The one defining the relation.
 *
 * ThroughModel: The intermediate model aka the join table. Like a User_x_Group.
 * You may use DefaultThroughModel if you don't care about additional data, and you have non-composite keys.
 *
 * ThroughFields: Ignore if using DefaultThroughModel. Otherwise provide the
 * mapping from ThroughModel's fields to the Target/Definer Models' fields.
 */
template <typename TargetModel, typename DefinerModel, typename ThroughModel, ThroughField... ThroughFields>
constexpr auto manyToManyRelation() {
  return ManyToManyRelation<TargetModel, DefinerModel, ThroughModel, ThroughFields...>();
};

} // namespace rukh::orm
