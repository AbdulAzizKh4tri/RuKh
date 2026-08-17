#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>
#include <rukh/TypeHelpers.hpp>

#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>

#include <rukh/orm/Column.hpp>
#include <rukh/orm/DeleteQuery.hpp>
#include <rukh/orm/InsertQuery.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/Relations.hpp>
#include <rukh/orm/SelectQuery.hpp>
#include <rukh/orm/UpdateQuery.hpp>
#include <rukh/orm/UpsertQuery.hpp>

// TODO: solve N+1 problem.
namespace rukh::orm {

template <typename Model, typename... PkTypes> class ActiveRecord {

public:
  static constexpr size_t pkArity = sizeof...(PkTypes);
  using PkTypesTuple = std::tuple<PkTypes...>;
  using PkType = std::tuple_element_t<0, PkTypesTuple>;

  operator PkType() const { return getPrimaryKeyValue(); }

  static Task<std::expected<std::optional<Model>, db::DatabaseError>> find(const PkTypesTuple &pkVal,
                                                                           db::ITransaction *transaction = nullptr) {
    co_return co_await SelectQuery<Model>().where(buildPkPredicate(pkVal)).first(transaction);
  }

  static SelectQuery<Model> queryAll(const std::string tableAlias = "") {
    if (tableAlias.empty())
      return SelectQuery<Model>();
    return SelectQuery<Model>(tableAlias);
  }
  static SelectQuery<Model> filter(const Predicate<Model> &p, const std::string tableAlias = "") {
    if (tableAlias.empty())
      return SelectQuery<Model>().where(p);
    return SelectQuery<Model>(tableAlias).where(p);
  }

  static Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  bulkInsert(const std::vector<Model> &objs, bool returning = false, db::ITransaction *transaction = nullptr) {
    auto result = co_await InsertQuery<Model>().execute(objs, transaction, returning);
    if (not result)
      co_return std::unexpected(result.error());
    co_return *result;
  }

  template <typename ConflictFieldsTuple, typename UpdateFieldsTuple>
  static Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  bulkUpsert(const std::vector<Model> &objs, const ConflictFieldsTuple &conflictFields, const bool doNothingOnConflict,
             const UpdateFieldsTuple &updateFields = std::tuple{}, bool returning = false,
             db::ITransaction *transaction = nullptr) {
    UpsertQuery<Model> query;
    std::apply([&](auto &&...col) { (query.conflictFields(col), ...); }, conflictFields);

    if (doNothingOnConflict)
      query.onConflictDoNothing();
    else
      std::apply([&](auto &&...col) { (query.updateFields(col), ...); }, updateFields);

    auto result = co_await query.execute(objs, transaction, returning);
    if (not result)
      co_return std::unexpected(result.error());
    co_return *result;
  }

  template <typename FieldTuple>
  static Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  bulkUpdate(const Model &newObj, FieldTuple fields, const Predicate<Model> &p, bool returning = false,
             db::ITransaction *transaction = nullptr) {
    UpdateQuery<Model> query;
    std::apply([&](auto &&...col) { (query.field(col), ...); }, fields);
    auto result = co_await query.where(p).execute(newObj, transaction, returning);
    if (not result)
      co_return std::unexpected(result.error());
    co_return *result;
  }

  static Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  bulkDestroy(const Predicate<Model> &p, bool returning = false, db::ITransaction *transaction = nullptr) {
    DeleteQuery<Model> query;
    auto result = co_await query.where(p).execute(transaction, returning);
    if (not result)
      co_return std::unexpected(result.error());
    co_return *result;
  }

  /*
   * Do not reuse Model objects after a rollback!
   * If called with a transaction that is later rolled back, this object's id and persisted_ will not reflect the
   * rollback.
   */
  Task<std::expected<size_t, db::DatabaseError>> save(db::ITransaction *transaction = nullptr) {
    if (persisted_)
      return update(transaction);
    return insert(transaction);
  }

  template <typename ConflictFieldsTuple>
  Task<std::expected<size_t, db::DatabaseError>> upsert(ConflictFieldsTuple conflictFields,
                                                        bool doNothingOnConflict = false,
                                                        db::ITransaction *transaction = nullptr) {
    Model *self = static_cast<Model *>(this);
    UpsertQuery<Model> query;
    std::apply([&](auto &&...col) { (query.conflictFields(col), ...); }, conflictFields);
    std::vector<Model> inputObjs{*self};

    if (doNothingOnConflict)
      query.onConflictDoNothing();

    auto result = co_await query.execute(inputObjs, transaction, true);
    if (not result)
      co_return std::unexpected(result.error());

    auto [affectedRowCount, objs] = *result;

    if (not objs.empty()) {
      setPersisted();
      *self = objs[0];
    }

    co_return affectedRowCount;
  }

  Task<std::expected<size_t, db::DatabaseError>> insert(db::ITransaction *transaction = nullptr) {
    Model *self = static_cast<Model *>(this);
    InsertQuery<Model> query;
    std::vector<Model> inputObjs{*self};

    auto result = co_await query.execute(inputObjs, transaction, true);
    if (not result)
      co_return std::unexpected(result.error());

    auto [affectedRowCount, objs] = *result;

    if (not objs.empty()) {
      setPersisted();
      *self = objs[0];
    }

    co_return affectedRowCount;
  }

  Task<std::expected<size_t, db::DatabaseError>> update(db::ITransaction *transaction = nullptr) {
    Predicate<Model> p = buildPkPredicate(this->getPrimaryKeyValues());
    Model *self = static_cast<Model *>(this);
    auto result = co_await UpdateQuery<Model>().where(p).execute(*self, transaction, true);
    if (not result)
      co_return std::unexpected(result.error());
    auto [affectedRowCount, objs] = *result;

    if (not objs.empty()) {
      *self = objs[0];
    }

    co_return affectedRowCount;
  }

  Task<std::expected<size_t, db::DatabaseError>> destroy(db::ITransaction *transaction = nullptr) {
    Predicate<Model> p = buildPkPredicate(this->getPrimaryKeyValues());
    Model *self = static_cast<Model *>(this);
    auto result = co_await DeleteQuery<Model>().where(p).execute(transaction, true);
    if (not result)
      co_return std::unexpected(result.error());

    auto [affectedRowCount, objs] = *result;
    if (not objs.empty())
      *self = objs[0];

    resetPersisted();

    co_return affectedRowCount;
  }

  Task<std::expected<bool, db::DatabaseError>> reload(db::ITransaction *transaction = nullptr) {
    Model *self = static_cast<Model *>(this);
    auto result = co_await find(getPrimaryKeyValues(), transaction);
    if (not result)
      co_return std::unexpected(result.error());
    if (not *result)
      co_return false;
    *self = result->value();
    co_return true;
  }

  template <FixedString RelationName = "", typename RelatedModel, typename ThroughModel>
  static Task<std::expected<size_t, db::DatabaseError>> upsertRelationThrough(ThroughModel throughObj,
                                                                              db::ITransaction *transaction = nullptr) {
    constexpr RelationLookup relLookup = getManyToManyRelation<RelationName, RelatedModel>();
    constexpr auto relation = relLookup.relation;
    using Relation = decltype(relation);
    using Through = Relation::Through;
    static_assert(std::is_same_v<Through, ThroughModel>,
                  "Provided through object does not match Relation's ThroughModel");

    constexpr SymmetryMode symmetryMode = relation.symmetryMode;
    constexpr auto throughFields = Relation::throughFields;
    constexpr auto conflictFields = Relation::getThroughFieldPtrs();

    if constexpr (symmetryMode == SymmetryMode::NONE or symmetryMode == SymmetryMode::DB_DOUBLE_ROW) {
      // non self-ref : upsert the obj as is
      // self ref asymmetric : upsert the obj as is
      // db double: upsert the obj as is.
      co_return co_await throughObj.upsert(conflictFields, false, transaction);
    } else if constexpr (symmetryMode == SymmetryMode::DB_SINGLE_ROW) {
      // db single: create new through model with pkA < pkB
      constexpr auto throughFieldsA = Relation::template getThroughFields<ThroughPtrModel::A>();
      constexpr auto throughFieldsB = Relation::template getThroughFields<ThroughPtrModel::B>();
      constexpr size_t fieldCountA = std::tuple_size_v<decltype(throughFieldsA)>;
      constexpr size_t fieldCountB = std::tuple_size_v<decltype(throughFieldsB)>;
      static_assert(fieldCountA == fieldCountB,
                    "Symmetric relations must have the same number of through fields for ModelA and ModelB");

      bool decided = false;
      bool correctOrder = true;

      [&]<size_t... I>(std::index_sequence<I...>) { // Checking if pkA < pkB
        (
            [&] {
              if (decided)
                return;
              const auto aValue = throughObj.*((std::get<I>(throughFieldsA)).throughPtr);
              const auto bValue = throughObj.*((std::get<I>(throughFieldsB)).throughPtr);
              if (aValue < bValue) {
                decided = true;
                correctOrder = true;
              } else if (bValue < aValue) {
                decided = true;
                correctOrder = false;
              }
            }(),
            ...);
      }(std::make_index_sequence<fieldCountA>{});

      Through correctThrough = throughObj;
      if (not correctOrder) {
        correctThrough = Relation::mirrorThroughObj(throughObj);
      }

      co_return co_await correctThrough.upsert(conflictFields, false, transaction);
    } else if constexpr (symmetryMode == SymmetryMode::SINGLE_ROW) {
      // single_row : check which version exists, upsert that one.

      // check mirror's existence
      Through mirror = Relation::mirrorThroughObj(throughObj);
      SelectQuery<Through> query;
      using Pred = Predicate<Through>;
      Pred wherePred(true);
      std::apply(
          [&](auto &&...flds) {
            ((wherePred = wherePred and Pred::equals(flds.throughPtr, mirror.*(flds.throughPtr))), ...);
          },
          throughFields);

      query.where(wherePred);

      const auto existResult = co_await query.exists(transaction);
      if (not existResult)
        co_return std::unexpected(existResult.error());

      if (*existResult) // this row exists already, upsert it.
        co_return co_await mirror.upsert(conflictFields, false, transaction);
      else
        co_return co_await throughObj.upsert(conflictFields, false, transaction);
    } else {
      // double_row: upsert both obj and mirror
      Through mirror = Relation::mirrorThroughObj(throughObj);

      const auto buResult =
          co_await Through::bulkUpsert({throughObj, mirror}, conflictFields, false, std::tuple{}, false, transaction);
      if (not buResult)
        co_return std::unexpected(buResult.error());

      co_return buResult->first;
    }
  }

  template <FixedString RelationName = "", typename RelatedModel>
  Task<std::expected<size_t, db::DatabaseError>> addRelation(const RelatedModel &other,
                                                             db::ITransaction *transaction = nullptr) {
    constexpr RelationLookup relLookup = getManyToManyRelation<RelationName, RelatedModel>();
    constexpr auto relation = relLookup.relation;
    using Relation = decltype(relation);
    using Through = Relation::Through;

    constexpr auto throughFields = Relation::throughFields;
    constexpr SymmetryMode symmetryMode = relation.symmetryMode;
    const Model *self = static_cast<Model *>(this);

    constexpr bool selfIsA = relLookup.callerIsA;
    constexpr bool otherIsB = relLookup.otherIsB;

    constexpr auto conflictFields = Relation::getThroughFieldPtrs();

    Through throughObj;

    if constexpr (symmetryMode == SymmetryMode::SINGLE_ROW) { // check whether relationship already exists
      SelectQuery<Through> query;

      using Pred = Predicate<Through>;
      Pred wherePred(true);
      std::apply(
          [&](auto &&...flds) {
            ((wherePred = wherePred and Pred::equals(flds.throughPtr, self->*(flds.modelPtr))), ...);
          },
          Relation::template getThroughFields<ThroughPtrModel::A>());
      std::apply(
          [&](auto &&...flds) {
            ((wherePred = wherePred and Pred::equals(flds.throughPtr, other.*(flds.modelPtr))), ...);
          },
          Relation::template getThroughFields<ThroughPtrModel::B>());

      Pred mirrorPred(true);
      std::apply(
          [&](auto &&...flds) {
            ((mirrorPred = mirrorPred and Pred::equals(flds.throughPtr, self->*(flds.modelPtr))), ...);
          },
          Relation::template getThroughFields<ThroughPtrModel::B>()); // flipped
      std::apply(
          [&](auto &&...flds) {
            ((mirrorPred = mirrorPred and Pred::equals(flds.throughPtr, other.*(flds.modelPtr))), ...);
          },
          Relation::template getThroughFields<ThroughPtrModel::A>());

      query.where(wherePred or mirrorPred);

      const auto result = co_await query.exists(transaction);
      if (not result)
        co_return std::unexpected(result.error());
      const auto exists = *result;
      if (exists)
        co_return 0;
    }

    // populate through object
    if constexpr (symmetryMode == SymmetryMode::DB_SINGLE_ROW or symmetryMode == SymmetryMode::SINGLE_ROW) {
      const bool selfSmaller = isSelfSmallerThan<Relation>(other);
      std::apply(
          [&](auto &&...flds) {
            ((throughObj.*(flds.throughPtr) = selfSmaller ? self->*(flds.modelPtr) : other.*(flds.modelPtr)), ...);
          },
          Relation::template getThroughFields<ThroughPtrModel::A>());

      std::apply(
          [&](auto &&...flds) {
            ((throughObj.*(flds.throughPtr) = selfSmaller ? other.*(flds.modelPtr) : self->*(flds.modelPtr)), ...);
          },
          Relation::template getThroughFields<ThroughPtrModel::B>());
    } else {
      // non-symmetric, non-self-ref, double_row and db_double_row
      std::apply([&](auto &&...flds) { ((throughObj.*(flds.throughPtr) = self->*(flds.modelPtr)), ...); },
                 Relation::template getThroughFields<selfIsA ? ThroughPtrModel::A : ThroughPtrModel::B>());

      std::apply([&](auto &&...flds) { ((throughObj.*(flds.throughPtr) = other.*(flds.modelPtr)), ...); },
                 Relation::template getThroughFields<otherIsB ? ThroughPtrModel::B : ThroughPtrModel::A>());
    }

    if constexpr (symmetryMode == SymmetryMode::DOUBLE_ROW) {
      Through mirrorThroughObj;
      std::apply([&](auto &&...flds) { ((mirrorThroughObj.*(flds.throughPtr) = self->*(flds.modelPtr)), ...); },
                 Relation::template getThroughFields<not selfIsA ? ThroughPtrModel::A : ThroughPtrModel::B>());
      std::apply([&](auto &&...flds) { ((mirrorThroughObj.*(flds.throughPtr) = other.*(flds.modelPtr)), ...); },
                 Relation::template getThroughFields<not otherIsB ? ThroughPtrModel::B : ThroughPtrModel::A>());

      const auto buResult = co_await Through::bulkUpsert({throughObj, mirrorThroughObj}, conflictFields, true,
                                                         std::tuple{}, false, transaction);
      if (not buResult)
        co_return std::unexpected(buResult.error());
      co_return buResult->first;
    }

    const auto upResult = co_await throughObj.upsert(conflictFields, true, transaction);
    if (not upResult)
      co_return std::unexpected(upResult.error());
    co_return *upResult;
  }

  template <FixedString RelationName = "", typename RelatedModel>
  Task<std::expected<size_t, db::DatabaseError>> removeRelation(const RelatedModel &other,
                                                                db::ITransaction *transaction = nullptr) {

    static constexpr RelationLookup relLookup = getManyToManyRelation<RelationName, RelatedModel>();
    static constexpr auto relation = relLookup.relation;

    static_assert(not relLookup.isReciprocal, "Cannot remove reciprocal relation. use forward name");

    using Relation = decltype(relation);
    using Through = Relation::Through;

    constexpr bool selfIsA = relLookup.callerIsA;
    constexpr bool otherIsB = relLookup.otherIsB;
    constexpr SymmetryMode symmetryMode = relation.symmetryMode;
    constexpr bool isSymmetric = symmetryMode != SymmetryMode::NONE;

    const Model *self = static_cast<Model *>(this);

    DeleteQuery<Through> query;
    using Pred = Predicate<Through>;

    Pred wherePred(true);
    // OKAY for self-ref asymmetric because the direction is A -> B, and callerIsA is always true in that case.
    // No need for reciprocal name.
    std::apply(
        [&](auto &&...flds) {
          ((wherePred = wherePred and Pred::equals(flds.throughPtr, self->*(flds.modelPtr))), ...);
        },
        Relation::template getThroughFields<selfIsA ? ThroughPtrModel::A : ThroughPtrModel::B>());
    std::apply(
        [&](auto &&...flds) {
          ((wherePred = wherePred and Pred::equals(flds.throughPtr, other.*(flds.modelPtr))), ...);
        },
        Relation::template getThroughFields<otherIsB ? ThroughPtrModel::B : ThroughPtrModel::A>());

    query.where(wherePred);

    // Adding DB_DOUBLE_ROW here because deleting mirror row is DB's responsibility in that case.
    if constexpr (isSymmetric and symmetryMode != SymmetryMode::DB_DOUBLE_ROW) {
      Pred mirrorPred(true);
      std::apply(
          [&](auto &&...flds) {
            ((mirrorPred = mirrorPred and Pred::equals(flds.throughPtr, self->*(flds.modelPtr))), ...);
          },
          Relation::template getThroughFields<ThroughPtrModel::B>());
      std::apply(
          [&](auto &&...flds) {
            ((mirrorPred = mirrorPred and Pred::equals(flds.throughPtr, other.*(flds.modelPtr))), ...);
          },
          Relation::template getThroughFields<ThroughPtrModel::A>());

      query.orWhere(mirrorPred);
    }
    auto delResult = co_await query.execute(transaction);
    if (not delResult)
      co_return std::unexpected(delResult.error());
    co_return delResult->first;
  }

  template <typename Relation> bool isSelfSmallerThan(const Model &obj) const {
    const Model *self = static_cast<const Model *>(this);

    bool smaller = false;
    bool decided = false;

    [&]<size_t... I>(std::index_sequence<I...>) {
      (
          [&] {
            if (decided)
              return;

            const auto &lhs = self->*(std::get<I>(Relation::throughFields).modelPtr);
            const auto &rhs = obj.*(std::get<I>(Relation::throughFields).modelPtr);

            if (lhs < rhs) {
              decided = true;
              smaller = true;
            } else if (rhs < lhs) {
              decided = true;
            }
          }(),
          ...);
    }(std::make_index_sequence<Relation::throughFieldCount>{});

    return smaller;
  }

  template <FixedString RelationName = "", typename RelatedModel>
  auto manyRelated(db::ITransaction *transaction = nullptr) {
    static constexpr RelationLookup relLookup = getManyToManyRelation<RelationName, RelatedModel>();
    static constexpr auto relation = relLookup.relation;
    using Relation = decltype(relation);
    using Through = Relation::Through;

    constexpr auto throughFields = Relation::throughFields;
    constexpr SymmetryMode symmetryMode = relation.symmetryMode;

    const Model *self = static_cast<Model *>(this);

    SelectQuery<RelatedModel, Through, Model> query("R");
    using Pred = Predicate<RelatedModel, Through, Model>;
    Pred joinPred(true);

    // If the relation is self-ref, callerIsA is always true, the condition asks whether the relation is reciprocal,
    // which is always false for symmetric self-refs, because symmetric self-refs do not have a reciprocal.
    constexpr bool condition = relLookup.isSelfRef ? not relLookup.isReciprocal : relLookup.callerIsA;

    std::apply(
        [&](auto &&...flds) {
          ((joinPred = joinPred and Pred::equals(flds.modelPtr, flds.throughPtr, {"R", "T"})), ...);
        },
        Relation::template getThroughFields<condition ? ThroughPtrModel::B : ThroughPtrModel::A>());

    Pred wherePred(true);
    std::apply(
        [&](auto &&...flds) {
          ((wherePred = wherePred and Pred::equals(flds.throughPtr, self->*flds.modelPtr, "T")), ...);
        },
        Relation::template getThroughFields<condition ? ThroughPtrModel::A : ThroughPtrModel::B>());

    if constexpr (symmetryMode == SymmetryMode::SINGLE_ROW or symmetryMode == SymmetryMode::DB_SINGLE_ROW) {
      Pred joinPred2(true);
      std::apply(
          [&](auto &&...flds) {
            ((joinPred2 = joinPred2 and Pred::equals(flds.modelPtr, flds.throughPtr, {"R", "T"})), ...);
          },
          Relation::template getThroughFields<ThroughPtrModel::B>());

      query.template join<Through>(joinPred or joinPred2, "T");

      Pred wherePred2(true);
      std::apply(
          [&](auto &&...flds) {
            ((wherePred2 = wherePred2 and Pred::equals(flds.throughPtr, self->*flds.modelPtr, "T")), ...);
          },
          Relation::template getThroughFields<ThroughPtrModel::A>());

      query.where(wherePred or wherePred2);
      return query;
    }

    query.template join<Through>(joinPred, "T");
    query.where(wherePred);
    return query;
  }

  template <typename Relation> struct RelationLookup {
    Relation relation;
    bool isReciprocal;
    bool isSelfRef;
    bool callerIsA;
    bool otherIsB;
  };

  template <FixedString RelationName = "", typename RelatedModel> static constexpr auto getManyToManyRelation() {
    static constexpr auto relTupleSelf = getManyToManyRelationByName<RelationName>();
    static constexpr auto relTupleOther = RelatedModel::template getManyToManyRelationByName<RelationName>();

    static constexpr size_t relTupleSelfSize = std::tuple_size_v<decltype(relTupleSelf)>;
    static constexpr size_t relTupleOtherSize = std::tuple_size_v<decltype(relTupleOther)>;

    static_assert(relTupleSelfSize == 2 or relTupleOtherSize == 2, "No relation found with that name.");
    static_assert(std::is_same_v<Model, RelatedModel> or not(relTupleSelfSize == 2 and relTupleOtherSize == 2),
                  "Multiple relations found with that name.");

    static constexpr auto relation = [&] {
      if constexpr (relTupleSelfSize == 2) {
        return std::get<0>(relTupleSelf);
      } else {
        return std::get<0>(relTupleOther);
      }
    }();
    static constexpr bool isReciprocal = [&] {
      if constexpr (relTupleSelfSize == 2) {
        return std::get<1>(relTupleSelf);
      } else {
        return std::get<1>(relTupleOther);
      }
    }();

    using Relation = decltype(relation);
    using A = Relation::A;
    using B = Relation::B;

    static_assert((std::is_same_v<A, Model> and std::is_same_v<B, RelatedModel>) or
                      (std::is_same_v<A, RelatedModel> and std::is_same_v<B, Model>),
                  "ManyToManyRelation was found, but there was a type mismatch. Check your template params");

    static constexpr RelationLookup r{
        .relation = relation,
        .isReciprocal = isReciprocal,
        .isSelfRef = std::is_same_v<A, B>,
        .callerIsA = std::is_same_v<A, Model>,
        .otherIsB = std::is_same_v<B, RelatedModel>,
    };
    return r;
  }

  template <FixedString RelationName = ""> static constexpr auto getManyToManyRelationByName() {
    static constexpr auto result = []<size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getManyToManyRelationIfNamed<RelationName, I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(Model::manyToManyRelations())>>{});
    static_assert(std::tuple_size_v<decltype(result)> <= 2, "Multiple relations found with that name.");
    return result;
  }

  template <FixedString RelationName = "", size_t I> static constexpr auto getManyToManyRelationIfNamed() {
    static constexpr auto rel = std::get<I>(Model::manyToManyRelations());
    if constexpr (rel.relationName == RelationName.view())
      return std::tuple{rel, false};
    else if constexpr (rel.reciprocalName == RelationName.view())
      return std::tuple{rel, true};
    else
      return std::tuple<>();
  }

  /*
   * Returns a SelectQuery<DefinerModel>.where(foreignKeyFields = this->primaryKeyFields)
   * For one-to-many and one-to-one relationships where the caller object's Model is referred to by a foreign key in
   * the DefinerModel. RelatedName need not be specified if there exists only one relation to this Model on the
   * DefinerModel.
   */
  template <typename DefinerModel, FixedString RelatedName = "">
  SelectQuery<DefinerModel> related(db::ITransaction *transaction = nullptr) {
    using Pd = Predicate<DefinerModel>;
    Pd p = Pd(true);

    static constexpr auto relationTuple = [] {
      if constexpr (RelatedName.view() == "")
        return DefinerModel::template getFkRelationByTargetModel<Model>();
      else
        return DefinerModel::template getFkRelationByName<RelatedName>();
    }();

    constexpr auto relationCount = std::tuple_size_v<decltype(relationTuple)>;
    static_assert(relationCount > 0, "related<>(): No matching Relation found on DefinerModel.");
    static_assert(
        relationCount < 2,
        "related<>(): Multiple matching Relations found on DefinerModel. Specify RelatedName template parameter.");

    constexpr auto relation = std::get<0>(relationTuple);
    constexpr auto comparisonFields = relation.getFieldPtrPairs();

    Model *self = static_cast<Model *>(this);
    SelectQuery<DefinerModel> query;

    std::apply(
        [&](auto &&...fieldPtrPairs) {
          auto addPredicate = [&](auto &&fieldPtrPair) {
            query.andWhere(Pd::equals(fieldPtrPair.fkPtr, self->*fieldPtrPair.pkPtr));
          };
          (addPredicate(fieldPtrPairs), ...);
        },
        comparisonFields);

    return query;
  }

  /*
   * Returns a SelectQuery<TargetModel>.where(primaryKeyFields = this->foreignKeyFields)
   * For fetching objects that this object refers to with foreign key fields
   * fkFieldPtrs are not needed if there is only one relation from this Model to TargetModel
   */
  template <typename TargetModel, auto... fkfieldPtrs>
  SelectQuery<TargetModel> ref(db::ITransaction *transaction = nullptr) {
    static constexpr auto relationTuple = [&] {
      if constexpr (sizeof...(fkfieldPtrs) > 0) {
        return getRelationByForeignKeyFieldPtrs<fkfieldPtrs...>();
      } else {
        return getFkRelationByTargetModel<TargetModel>();
      }
    }();

    constexpr auto relationCount = std::tuple_size_v<decltype(relationTuple)>;
    static_assert(relationCount > 0, "ref<>(): No matching Relation found.");
    static_assert(relationCount < 2,
                  "ref<>(): Multiple matching Relations found. Specify using field pointers to disambiguate.");

    constexpr auto relation = std::get<0>(relationTuple);
    constexpr auto comparisonFields = relation.getFieldPtrPairs();

    const Model *self = static_cast<const Model *>(this);

    using Pt = Predicate<TargetModel>;
    SelectQuery<TargetModel> query;
    bool found = false;
    std::apply(
        [&](auto &&...fieldPtrPairs) {
          auto addPredicate = [&](auto &&fieldPtrPair) {
            auto fkField = self->*fieldPtrPair.fkPtr;
            if constexpr (OptionalT<std::remove_cvref_t<decltype(fkField)>>) {
              if (not fkField) {
                query.andWhere(Pt(false));
                return;
              }
              query.andWhere(Pt::equals(fieldPtrPair.pkPtr, *fkField));
            } else {
              query.andWhere(Pt::equals(fieldPtrPair.pkPtr, fkField));
            }
          };
          (addPredicate(fieldPtrPairs), ...);
        },
        comparisonFields);

    return query;
  }

  static constexpr auto fkRelations() {
    constexpr auto rels = Model::relations();
    static constexpr auto result = []<size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getRelationIfFk<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(rels)>>{});
    return result;
  }

  static constexpr auto manyToManyRelations() {
    constexpr auto rels = Model::relations();
    static constexpr auto result = []<size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getRelationIfManyToMany<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(rels)>>{});
    return result;
  }

  PkType getPrimaryKeyValue() const {
    auto pkTuple = pkColumns();
    const Model *self = static_cast<const Model *>(this);
    return self->*std::get<0>(pkTuple).fieldPtr;
  }

  PkTypesTuple getPrimaryKeyValues() const {
    auto cols = pkColumns();
    const Model *self = static_cast<const Model *>(this);
    return [&, this]<size_t... I>(std::index_sequence<I...>) {
      return PkTypesTuple{(self->*std::get<I>(cols).fieldPtr)...};
    }(std::make_index_sequence<pkArity>{});
  }

  /*
   * Returns the Database column name of a Model field pointer
   */
  template <typename FieldT> static std::string columnNameOf(FieldT Model::*fieldPtr) {
    std::string result;
    bool found = false;
    std::apply(
        [&](auto &&...col) {
          auto check = [&](auto &&c) {
            using ColPtr = std::remove_cvref_t<decltype(c.fieldPtr)>;
            using ArgPtr = std::remove_cvref_t<decltype(fieldPtr)>;

            if constexpr (std::is_same_v<ColPtr, ArgPtr>) {
              if (c.fieldPtr == fieldPtr) {
                result = c.dbName;
                found = true;
              }
            }
          };
          (check(col), ...);
        },
        Model::columns());
    if (not found)
      throw rukh::OrmException("Invalid field ptr. Model Field does not exist or is not registered.");
    return result;
  }

  static constexpr bool isValidColumnName(const std::string &name) {
    return std::apply([&](auto &&...col) { return ((col.dbName == name) or ...); }, Model::columns());
  }

  static constexpr auto pkFieldPtr() { return std::get<0>(pkColumns()).fieldPtr; }

  static constexpr auto pkFieldPtrs() {
    static constexpr auto result = []<size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getFieldPtrIfPk<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(Model::columns())>>{});
    return result;
  }

  static constexpr auto pkColumns() {
    static constexpr auto result = []<size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getColumnIfPk<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(Model::columns())>>{});
    return result;
  }

  template <typename LookupModel> static constexpr auto getFkRelationByTargetModel() {
    return []<size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getFkRelationIfTargetModel<LookupModel, I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(fkRelations())>>{});
  }

  template <FixedString Name> static constexpr auto getFkRelationByName() {
    return []<size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getFkRelationIfNamed<Name, I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(fkRelations())>>{});
  }

  template <FieldPointer FieldPtr> static Column<Model, FieldPtr> getColumnObject(FieldPtr Model::*fieldPtr) {
    Column<Model, FieldPtr> result;
    bool found = false;
    std::apply(
        [&](auto &&...col) {
          auto check = [&](auto &&c) {
            using ColPtr = std::remove_cvref_t<decltype(c.fieldPtr)>;
            using ArgPtr = std::remove_cvref_t<decltype(fieldPtr)>;
            if constexpr (std::is_same_v<ColPtr, ArgPtr>) {
              if (c.fieldPtr == fieldPtr) {
                result = c;
                found = true;
              }
            }
          };
          (check(col), ...);
        },
        Model::columns());
    if (not found)
      throw OrmException("Field does not belong to Model");
    return result;
  }

  /*
   * Stringifies the JSON representation of the Model.
   * Indent parameter is passed to nlohmann::json::dump
   */
  std::string toString(const int indent = -1) const { return toJson().dump(indent); }

  /*
   * Default serialization mode is AUTO.
   * For more complex fields, Json serializers can be set per Column object.
   * Or you can define your own toJson() function.
   */
  nlohmann::json toJson() const {
    nlohmann::json j;
    const Model *self = static_cast<const Model *>(this);
    std::apply(
        [&](auto &&...cols) {
          auto addToJ = [self, &j](auto &&col) {
            using FieldType = std::remove_cvref_t<decltype(self->*col.fieldPtr)>;

            if (col.jsonSerializationMode == JsonSerializationMode::OFF)
              return;

            if (col.jsonSerializationMode == JsonSerializationMode::CUSTOM) {
              if (auto result = col.jsonSerializer(self->*col.fieldPtr))
                j[col.dbName] = *result;
              // nullopt from the serializer -> omit the key
              return;
            }

            if constexpr (OptionalT<FieldType>) {
              // optional field
              if (self->*col.fieldPtr)
                j[col.dbName] = *(self->*col.fieldPtr);
            } else {
              // non-optional field
              j[col.dbName] = self->*col.fieldPtr;
            }
          };
          (addToJ(cols), ...);
        },
        Model::columns());
    return j;
  }

  void setPersisted() { persisted_ = true; }
  bool isPersisted() { return persisted_; }
  void resetPersisted() { persisted_ = false; }

private:
  bool persisted_ = false;

  //==============================PK HELPERS==============================
  template <size_t I> static constexpr auto getColumnIfPk() {
    constexpr auto col = std::get<I>(Model::columns());
    if constexpr (col.isPrimaryKey)
      return std::tuple{std::get<I>(Model::columns())};
    else
      return std::tuple<>{};
  }

  template <size_t I> static constexpr auto getFieldPtrIfPk() {
    constexpr auto col = std::get<I>(Model::columns());
    if constexpr (col.isPrimaryKey)
      return std::tuple{std::get<I>(Model::columns()).fieldPtr};
    else
      return std::tuple<>{};
  }

  static Predicate<Model> buildPkPredicate(const PkTypesTuple &pkVal) {
    auto cols = pkColumns();
    return [&]<size_t... I>(std::index_sequence<I...>) {
      return (Predicate<Model>::equals(std::get<I>(cols).fieldPtr, std::get<I>(pkVal)) and ...);
    }(std::make_index_sequence<pkArity>{});
  }

  //==============================Relation Helpers==============================

  template <auto... fkfieldPtrs> static constexpr auto getRelationByForeignKeyFieldPtrs() {
    using FkRelationsTuple = std::remove_cvref_t<decltype(fkRelations())>;

    constexpr auto lookupFieldTuple = std::make_tuple(fkfieldPtrs...);
    using LookUpFieldTupleType = std::remove_cvref_t<decltype(lookupFieldTuple)>;

    return [&]<size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat([&]<size_t Idx>() constexpr {
        using Relation = std::tuple_element_t<Idx, FkRelationsTuple>;
        using RelFk = std::remove_cvref_t<decltype(std::get<Idx>(fkRelations()).fkFieldPtrs)>;
        if constexpr (std::same_as<RelFk, LookUpFieldTupleType>) {
          if constexpr (std::get<Idx>(fkRelations()).fkFieldPtrs == lookupFieldTuple) {
            return std::tuple<Relation>{std::get<Idx>(fkRelations())};
          } else {
            return std::tuple<>{};
          }
        } else {
          return std::tuple<>{};
        }
      }.template operator()<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<FkRelationsTuple>>{});
  }

  //==============================Many to Many==============================

  template <size_t I> static constexpr auto getRelationIfManyToMany() {
    using RelationsTuple = decltype(Model::relations());
    using RelT = std::tuple_element_t<I, RelationsTuple>;

    if constexpr (RelT::relationType == RelationType::MANY_TO_MANY)
      return std::tuple{std::get<I>(Model::relations())};
    else
      return std::tuple<>{};
  }

  //==============================ONE TO ONE/ MANY TO ONE==============================

  template <typename LookupModel, size_t I> static constexpr auto getFkRelationIfTargetModel() {
    using RelT = std::remove_cvref_t<decltype(std::get<I>(fkRelations()))>;
    if constexpr (std::same_as<typename RelT::Target, LookupModel>)
      return std::tuple{std::get<I>(fkRelations())};
    else
      return std::tuple{};
  }

  template <FixedString Name, size_t I> static constexpr auto getFkRelationIfNamed() {
    if constexpr (std::get<I>(fkRelations()).relatedName == Name.view())
      return std::tuple{std::get<I>(fkRelations())};
    else
      return std::tuple{};
  }

  template <size_t I> static constexpr auto getRelationIfFk() {
    using RelationsTuple = decltype(Model::relations());
    using RelT = std::tuple_element_t<I, RelationsTuple>;

    if constexpr (RelT::relationType == RelationType::ONE_TO_ONE or RelT::relationType == RelationType::MANY_TO_ONE)
      return std::tuple{std::get<I>(Model::relations())};
    else
      return std::tuple<>{};
  }
};
} // namespace rukh::orm
