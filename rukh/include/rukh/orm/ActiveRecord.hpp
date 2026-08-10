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

namespace rukh::orm {

enum class LookupDirection { FORWARD, REVERSE };
template <typename Model, typename... PkTypes> class ActiveRecord {

public:
  static constexpr std::size_t pkArity = sizeof...(PkTypes);
  using PkTypesTuple = std::tuple<PkTypes...>;
  using PkType = std::tuple_element_t<0, PkTypesTuple>;

  operator PkType() const { return getPrimaryKeyValue(); }

  static Task<std::expected<std::optional<Model>, db::DatabaseError>> find(const PkTypesTuple &pkVal,
                                                                           db::ITransaction *transaction = nullptr) {
    co_return co_await SelectQuery<Model>().where(buildPkPredicate(pkVal)).first(transaction);
  }

  static SelectQuery<Model> all() { return SelectQuery<Model>(); }
  static SelectQuery<Model> filter(const Predicate<Model> &p) { return SelectQuery<Model>().where(p); }

  static Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  bulkInsert(const std::vector<Model> &objs, db::ITransaction *transaction = nullptr) {
    auto result = co_await InsertQuery<Model>().execute(objs, transaction, true);
    if (not result)
      co_return std::unexpected(result.error());
    co_return *result;
  }

  template <typename FieldTuple>
  static Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  bulkUpdate(const Model &newObj, FieldTuple fields, const Predicate<Model> &p,
             db::ITransaction *transaction = nullptr) {
    UpdateQuery<Model> query;
    std::apply([&](auto &&...col) { (query.field(col), ...); }, fields);
    auto result = co_await query.where(p).execute(newObj, transaction, true);
    if (not result)
      co_return std::unexpected(result.error());
    co_return *result;
  }

  static Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  bulkDestroy(const Predicate<Model> &p, db::ITransaction *transaction = nullptr) {
    DeleteQuery<Model> query;
    auto result = co_await query.where(p).execute(transaction, true);
    if (not result)
      co_return std::unexpected(result.error());
    co_return *result;
  }

  /*
   * Do not reuse Model objects after a rollback!
   * If called with a transaction that is later rolled back, this object's id and persisted_ will not reflect the
   * rollback.
   */
  Task<std::expected<Model, db::DatabaseError>> save(db::ITransaction *transaction = nullptr) {
    if (persisted_)
      co_return co_await update(transaction);
    else
      co_return co_await insert(transaction);
  }

  template <typename FieldsTuple>
  Task<std::expected<Model, db::DatabaseError>> upsert(FieldsTuple fieldTuple,
                                                       db::ITransaction *transaction = nullptr) {
    Model *self = static_cast<Model *>(this);
    UpsertQuery<Model> query;
    std::apply([&](auto &&...col) { (query.conflictFields(col), ...); }, fieldTuple);
    std::vector<Model> inputObjs{*self};

    auto result = co_await query.execute(inputObjs, transaction, true);
    if (not result)
      co_return std::unexpected(result.error());

    auto [_, objs] = *result;

    *self = objs[0];
    setPersisted();
    co_return objs[0];
  }

  Task<std::expected<Model, db::DatabaseError>> insert(db::ITransaction *transaction = nullptr) {
    Model *self = static_cast<Model *>(this);
    InsertQuery<Model> query;
    std::vector<Model> inputObjs{*self};

    auto result = co_await query.execute(inputObjs, transaction, true);
    if (not result)
      co_return std::unexpected(result.error());

    auto [_, objs] = *result;

    *self = objs[0];
    setPersisted();
    co_return objs[0];
  }

  Task<std::expected<Model, db::DatabaseError>> update(db::ITransaction *transaction = nullptr) {
    Predicate<Model> p = buildPkPredicate(this->getPrimaryKeyValues());
    Model *self = static_cast<Model *>(this);
    auto result = co_await UpdateQuery<Model>().where(p).execute(*self, transaction, true);
    if (not result)
      co_return std::unexpected(result.error());
    auto [_, objs] = *result;

    *self = objs[0];
    co_return objs[0];
  }

  Task<std::expected<Model, db::DatabaseError>> destroy(db::ITransaction *transaction = nullptr) {
    Predicate<Model> p = buildPkPredicate(this->getPrimaryKeyValues());
    Model *self = static_cast<Model *>(this);
    auto result = co_await DeleteQuery<Model>().where(p).execute(transaction, true);
    if (not result)
      co_return std::unexpected(result.error());
    auto [_, objs] = *result;
    if (objs.empty())
      throw DatabaseException("Failed to delete object");
    *self = objs[0];
    co_return objs[0];
  }

  Task<std::expected<std::optional<Model>, db::DatabaseError>> reload(db::ITransaction *transaction = nullptr) {
    Model *self = static_cast<Model *>(this);
    auto result = co_await find(getPrimaryKeyValues(), transaction);
    if (not result)
      co_return std::unexpected(result.error());
    if (not *result)
      co_return std::nullopt;
    *self = **result;
    co_return **result;
  }

  template <typename RelatedModel, FixedString RelationName = "", typename ThroughModel>
  static Task<std::expected<size_t, db::DatabaseError>> upsertRelation(ThroughModel throughObj,
                                                                       db::ITransaction *transaction = nullptr) {
    static constexpr auto relation = getManyToManyRelation<RelatedModel, RelationName>();
    using Relation = decltype(relation);
    static_assert(std::is_same_v<typename Relation::Through, ThroughModel>,
                  "Provided Through object does not belong to Relation");
    constexpr auto throughFields = getManyToManyRelationThroughFields<Relation>();

    if constexpr (relation.symmetryMode == SymmetryMode::DOUBLE_ROW) {
      auto mirror = getMirrorThroughObject<Relation>(throughObj);
      UpsertQuery<ThroughModel> upsertQuery;
      std::apply([&](auto &&...flds) { (upsertQuery.conflictFields(flds.throughPtr), ...); }, throughFields);

      const auto upsertResult = co_await upsertQuery.execute(std::vector{throughObj, mirror}, transaction);
      if (not upsertResult)
        co_return std::unexpected(upsertResult.error());
      co_return (*upsertResult).first;
    } else {
      using Pred = Predicate<ThroughModel>;
      Pred pred(true);
      std::apply(
          [&](auto &&...flds) { ((pred = pred and Pred::equals(flds.throughPtr, throughObj.*flds.throughPtr)), ...); },
          throughFields);

      if constexpr (relation.symmetryMode == SymmetryMode::SINGLE_ROW) {
        Pred mirrorPred(true);
        auto mirror = getMirrorThroughObject<Relation>(throughObj);
        std::apply(
            [&](auto &&...flds) {
              ((mirrorPred = mirrorPred and Pred::equals(flds.throughPtr, mirror.*flds.throughPtr)), ...);
            },
            throughFields);

        pred = pred or mirrorPred;
      }

      SelectQuery<ThroughModel> throughQuery = ThroughModel::filter(pred);
      const auto firstResult = co_await throughQuery.first(transaction);
      if (not firstResult)
        co_return std::unexpected(firstResult.error());

      const auto firstOpt = *firstResult;
      if (firstOpt.has_value()) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          ((throughObj.*(std::get<I>(throughFields).throughPtr) = (*firstOpt).*std::get<I>(throughFields).throughPtr),
           ...);
        }(std::make_index_sequence<std::tuple_size_v<decltype(throughFields)>>{});
      }

      const auto conflictFieldPtrs = [&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::tuple_cat(std::tuple{std::get<I>(throughFields).throughPtr}...);
      }(std::make_index_sequence<std::tuple_size_v<decltype(throughFields)>>{});

      auto res = co_await throughObj.upsert(conflictFieldPtrs, transaction);
      if (not res)
        co_return std::unexpected(res.error());
      co_return 1;
    }
  }

  template <FixedString RelationName = "", typename RelatedModel>
  Task<std::expected<size_t, db::DatabaseError>> upsertRelation(const RelatedModel &relatedObj,
                                                                db::ITransaction *transaction = nullptr) {
    static constexpr auto relation = getManyToManyRelation<RelatedModel, RelationName>();
    using Relation = decltype(relation);
    using ThroughModel = Relation::Through;
    constexpr bool thisIsDefiner = std::is_same_v<typename Relation::Definer, Model>;
    constexpr auto throughFields = getManyToManyRelationThroughFields<Relation>();

    const std::string relatedAlias = "r";
    const std::string throughAlias = "t";
    using Pred = Predicate<ThroughModel>;
    const Model *self = static_cast<const Model *>(this);

    ThroughModel throughObj = [&] {
      if constexpr (thisIsDefiner)
        return buildThroughObject<Relation>(relatedObj, *self);
      else
        return buildThroughObject<Relation>(*self, relatedObj);
    }();

    Pred wherePred = (buildThroughWherePredicate<Relation, Pred, Model>(
                          self, thisIsDefiner ? ThroughPtrType::DEFINER : ThroughPtrType::TARGET, "") and
                      buildThroughWherePredicate<Relation, Pred, RelatedModel>(
                          &relatedObj, thisIsDefiner ? ThroughPtrType::TARGET : ThroughPtrType::DEFINER, ""));

    if constexpr (relation.symmetryMode == SymmetryMode::SINGLE_ROW) {
      wherePred = wherePred or
                  (buildThroughWherePredicate<Relation, Pred, Model>(self, ThroughPtrType::TARGET, "") and
                   buildThroughWherePredicate<Relation, Pred, RelatedModel>(&relatedObj, ThroughPtrType::DEFINER, ""));
    }

    SelectQuery<ThroughModel> throughQuery = ThroughModel::filter(wherePred);

    if constexpr (relation.symmetryMode == SymmetryMode::DOUBLE_ROW) {
      ThroughModel mirrorThroughObj = [&] {
        if constexpr (not thisIsDefiner)
          return buildThroughObject<Relation>(relatedObj, *self);
        else
          return buildThroughObject<Relation>(*self, relatedObj);
      }();

      auto biResult = co_await ThroughModel::bulkInsert({throughObj, mirrorThroughObj}, transaction);
      if (not biResult) {
        if (biResult.error().type == db::DbErrorType::DUPLICATE_KEY or
            biResult.error().type == db::DbErrorType::UNIQUE_CONSTRAINT_VIOLATION) {
          co_return 0;
        }
        co_return std::unexpected(biResult.error());
      }
      co_return (*biResult).first;
    } else {
      auto gcResult = co_await throughQuery.getOneOrCreate(throughObj, transaction);
      if (not gcResult)
        co_return std::unexpected(gcResult.error());
      auto [_, created] = *gcResult;
      co_return created ? 1 : 0;
    }
  }

  template <FixedString RelationName = "", typename RelatedModel>
  Task<std::expected<size_t, db::DatabaseError>> removeRelation(const RelatedModel &relatedObj,
                                                                db::ITransaction *transaction = nullptr) {
    static constexpr auto relation = getManyToManyRelation<RelatedModel, RelationName>();
    using Relation = decltype(relation);
    using ThroughModel = Relation::Through;
    constexpr bool thisIsDefiner = std::is_same_v<typename Relation::Definer, Model>;
    constexpr auto throughFields = getManyToManyRelationThroughFields<Relation>();

    const std::string relatedAlias = "r";
    const std::string throughAlias = "t";
    using Pred = Predicate<ThroughModel>;
    const Model *self = static_cast<const Model *>(this);

    Pred wherePred = (buildThroughWherePredicate<Relation, Pred, Model>(
                          self, thisIsDefiner ? ThroughPtrType::DEFINER : ThroughPtrType::TARGET, "") and
                      buildThroughWherePredicate<Relation, Pred, RelatedModel>(
                          &relatedObj, thisIsDefiner ? ThroughPtrType::TARGET : ThroughPtrType::DEFINER, ""));

    if constexpr (relation.symmetryMode == SymmetryMode::SINGLE_ROW or
                  relation.symmetryMode == SymmetryMode::DOUBLE_ROW) {
      wherePred = wherePred or
                  (buildThroughWherePredicate<Relation, Pred, Model>(self, ThroughPtrType::TARGET, "") and
                   buildThroughWherePredicate<Relation, Pred, RelatedModel>(&relatedObj, ThroughPtrType::DEFINER, ""));
    }

    auto delResult = co_await DeleteQuery<ThroughModel>().where(wherePred).execute(transaction);
    if (not delResult)
      co_return std::unexpected(delResult.error());

    co_return (*delResult).first;
  }

  template <typename RelatedModel, FixedString RelationName = "">
  auto manyRelated(LookupDirection direction = LookupDirection::FORWARD, db::ITransaction *transaction = nullptr) {
    static constexpr auto relation = getManyToManyRelation<RelatedModel, RelationName>();

    using Relation = decltype(relation);
    using ThroughModel = Relation::Through;
    constexpr bool thisIsDefiner = std::is_same_v<typename Relation::Definer, Model>;
    constexpr auto throughFields = getManyToManyRelationThroughFields<Relation>();

    using Query = SelectQuery<RelatedModel, Model, ThroughModel>;
    using Pred = Predicate<RelatedModel, Model, ThroughModel>;

    const std::string relatedAlias = "r";
    const std::string throughAlias = "t";
    const Model *self = static_cast<const Model *>(this);

    Query query(relatedAlias);

    // Join on the OTHER / Related / Queried  model. Where clause on THIS / calling model.
    Predicate joinPredicate = buildThroughJoinPredicate<Relation, RelatedModel, ThroughModel>(
        (thisIsDefiner and direction == LookupDirection::FORWARD) ? ThroughPtrType::TARGET : ThroughPtrType::DEFINER,
        throughAlias, relatedAlias);
    Predicate wherePredicate = buildThroughWherePredicate<Relation, Pred, Model>(
        self,
        (thisIsDefiner and direction == LookupDirection::FORWARD) ? ThroughPtrType::DEFINER : ThroughPtrType::TARGET,
        throughAlias);

    query.template join<ThroughModel>(joinPredicate, throughAlias).where(wherePredicate);

    if constexpr (relation.symmetryMode == SymmetryMode::SINGLE_ROW) {
      Query queryMirrored(relatedAlias);

      // thisIsDefiner will always be true for Self Referencing relationships.
      // WHERE on the OTHER / Related / Queried  model. JOIN on THIS / calling model. Mirror of what we did above.
      Predicate joinPredicateMirrored = buildThroughJoinPredicate<Relation, RelatedModel, ThroughModel>(
          (thisIsDefiner and direction == LookupDirection::FORWARD) ? ThroughPtrType::DEFINER : ThroughPtrType::TARGET,
          throughAlias, relatedAlias);
      Predicate wherePredicateMirrored = buildThroughWherePredicate<Relation, Pred, Model>(
          self,
          (thisIsDefiner and direction == LookupDirection::FORWARD) ? ThroughPtrType::TARGET : ThroughPtrType::DEFINER,
          throughAlias);
      queryMirrored.template join<ThroughModel>(joinPredicateMirrored, throughAlias).where(wherePredicateMirrored);
      query = query.unionAllQuery(queryMirrored);
    }

    return query;
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
    static constexpr auto result = []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getRelationIfFk<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(rels)>>{});
    return result;
  }

  static constexpr auto manyToManyRelations() {
    constexpr auto rels = Model::relations();
    static constexpr auto result = []<std::size_t... I>(std::index_sequence<I...>) {
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
    return [&, this]<std::size_t... I>(std::index_sequence<I...>) {
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
    static constexpr auto result = []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getFieldPtrIfPk<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(Model::columns())>>{});
    return result;
  }

  static constexpr auto pkColumns() {
    static constexpr auto result = []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getColumnIfPk<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(Model::columns())>>{});
    return result;
  }

  template <typename LookupModel> static constexpr auto getFkRelationByTargetModel() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getFkRelationIfTargetModel<LookupModel, I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(fkRelations())>>{});
  }

  template <FixedString Name> static constexpr auto getFkRelationByName() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getFkRelationIfNamed<Name, I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(fkRelations())>>{});
  }

  template <typename RelatedModel, FixedString RelationName> static constexpr auto getManyToManyRelation() {
    constexpr bool byName = (RelationName.view() != "");
    constexpr auto rels = [&] { // checking this model
      if constexpr (byName) {
        return getManyToManyRelationByName<RelationName>();
      } else {
        return getManyToManyRelationByTargetModel<RelatedModel>();
      }
    }();

    constexpr auto relSize = std::tuple_size_v<decltype(rels)>;
    static_assert(relSize < 2, "More than one many-to-many relation found with this model combination or "
                               "RelationName. Define Relations only once.");
    constexpr auto relsOther = [&] { // checking the other model
      if constexpr (byName) {
        return RelatedModel::template getManyToManyRelationByName<RelationName>();
      } else {
        return RelatedModel::template getManyToManyRelationByTargetModel<Model>();
      }
    }();

    constexpr auto relSizeOther = std::tuple_size_v<decltype(relsOther)>;
    static_assert(relSizeOther < 2, "More than one many-to-many relation found with this model combination or "
                                    "RelationName. Define Relations only once.");

    static_assert(std::is_same_v<Model, RelatedModel> or (relSize == 1 xor relSizeOther == 1),
                  "Relation lookup by Model is ambiguous in this case. Use RelationName");

    if constexpr (relSize == 1) {
      constexpr auto result = std::get<0>(rels);
      using Relation = decltype(result);

      static_assert(
          (std::is_same_v<typename Relation::Definer, Model> and
           std::is_same_v<typename Relation::Target, RelatedModel>) or
              (std::is_same_v<typename Relation::Target, Model> and
               std::is_same_v<typename Relation::Definer, RelatedModel>),
          "Relation found by name on this model, but RelatedModel does not match. check your template params.");

      return std::get<0>(rels);
    } else {
      constexpr auto result = std::get<0>(relsOther);
      using Relation = decltype(result);

      static_assert((std::is_same_v<typename Relation::Definer, Model> and
                     std::is_same_v<typename Relation::Target, RelatedModel>) or
                        (std::is_same_v<typename Relation::Target, Model> and
                         std::is_same_v<typename Relation::Definer, RelatedModel>),
                    "Relation found by name on other model, but it's not related to calling model. check your "
                    "template params.");
      return std::get<0>(relsOther);
    }
  }

  template <typename LookupModel> static constexpr auto getManyToManyRelationByTargetModel() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getManyToManyRelationIfTargetModel<LookupModel, I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(manyToManyRelations())>>{});
  }

  template <FixedString Name> static constexpr auto getManyToManyRelationByName() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getManyToManyRelationIfNamed<Name, I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(manyToManyRelations())>>{});
  }

  template <typename FieldPtr> static Column<Model, FieldPtr> getColumnObject(FieldPtr Model::*fieldPtr) {
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
  template <std::size_t I> static constexpr auto getColumnIfPk() {
    constexpr auto col = std::get<I>(Model::columns());
    if constexpr (col.isPrimaryKey)
      return std::tuple{std::get<I>(Model::columns())};
    else
      return std::tuple<>{};
  }

  template <std::size_t I> static constexpr auto getFieldPtrIfPk() {
    constexpr auto col = std::get<I>(Model::columns());
    if constexpr (col.isPrimaryKey)
      return std::tuple{std::get<I>(Model::columns()).fieldPtr};
    else
      return std::tuple<>{};
  }

  static Predicate<Model> buildPkPredicate(const PkTypesTuple &pkVal) {
    auto cols = pkColumns();
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return (Predicate<Model>::equals(std::get<I>(cols).fieldPtr, std::get<I>(pkVal)) and ...);
    }(std::make_index_sequence<pkArity>{});
  }

  //==============================Relation Helpers==============================

  template <auto... fkfieldPtrs> static constexpr auto getRelationByForeignKeyFieldPtrs() {
    using FkRelationsTuple = std::remove_cvref_t<decltype(fkRelations())>;

    constexpr auto lookupFieldTuple = std::make_tuple(fkfieldPtrs...);
    using LookUpFieldTupleType = std::remove_cvref_t<decltype(lookupFieldTuple)>;

    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat([&]<std::size_t Idx>() constexpr {
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

  template <typename Relation, typename RelatedModel, typename ThroughModel>
  auto buildThroughJoinPredicate(ThroughPtrType matchType, const std::string &throughAlias,
                                 const std::string &relatedAlias) {
    using Pred = Predicate<RelatedModel, Model, ThroughModel>;
    const Model *self = static_cast<const Model *>(this);
    Pred pred(true);
    std::apply(
        [&](auto &&...flds) {
          auto add = [&](auto &&fld) {
            if (fld.throughPtrType == matchType)
              pred = pred and Pred::equals(fld.throughPtr, fld.modelPtr, {throughAlias, relatedAlias});
          };
          (add(flds), ...);
        },
        getManyToManyRelationThroughFields<Relation>());
    return pred;
  };

  template <typename Relation, typename Pred, typename CheckModel>
  auto buildThroughWherePredicate(const CheckModel *objPtr, ThroughPtrType matchType, const std::string &throughAlias) {
    Pred p(true);
    std::apply(
        [&](auto &&...flds) {
          auto add = [&](auto &&throughfld) {
            using ThroughFieldModelPtr = get_class_t<std::remove_cvref_t<decltype(throughfld.modelPtr)>>;
            if constexpr (std::is_same_v<ThroughFieldModelPtr, CheckModel>) {
              if (throughfld.throughPtrType == matchType) {
                p = p and Pred::equals(throughfld.throughPtr, objPtr->*throughfld.modelPtr, throughAlias);
              }
            }
          };
          (add(flds), ...);
        },
        getManyToManyRelationThroughFields<Relation>());
    return p;
  };

  template <typename Relation, typename TargetModel = Relation::Target, typename DefinerModel = Relation::Definer>
  typename Relation::Through buildThroughObject(const TargetModel &tObj, const DefinerModel &dObj) {
    typename Relation::Through obj;

    std::apply(
        [&](auto &&...flds) {
          auto add = [&](auto &&throughfld) {
            using ThroughFieldModelType = get_class_t<std::remove_cvref_t<decltype(throughfld.modelPtr)>>;
            if (throughfld.throughPtrType == ThroughPtrType::TARGET) {
              if constexpr (std::is_same_v<ThroughFieldModelType, TargetModel>)
                obj.*throughfld.throughPtr = tObj.*throughfld.modelPtr;
            } else {
              if constexpr (std::is_same_v<ThroughFieldModelType, DefinerModel>)
                obj.*throughfld.throughPtr = dObj.*throughfld.modelPtr;
            }
          };
          (add(flds), ...);
        },
        getManyToManyRelationThroughFields<Relation>());

    return obj;
  };

  template <typename Relation> static constexpr auto getManyToManyRelationDefinerThroughFields() {
    using ThroughModel = Relation::Through;
    using DefinerModel = Relation::Definer;
    if constexpr (Relation::throughFieldCount == 0) {
      constexpr auto throughField =
          ThroughField(&ThroughModel::definerPk, DefinerModel::pkFieldPtr(), ThroughPtrType::DEFINER);
      return std::tuple{throughField};
    } else {
      static constexpr auto result = []<std::size_t... I>(std::index_sequence<I...>) {
        return std::tuple_cat(getThroughFieldIfDefiner<I, Relation>()...);
      }(std::make_index_sequence<std::tuple_size_v<decltype(Relation::throughFields)>>{});
      return result;
    }
  }

  template <typename Relation> static constexpr auto getManyToManyRelationTargetThroughFields() {
    using ThroughModel = Relation::Through;
    using TargetModel = Relation::Target;
    if constexpr (Relation::throughFieldCount == 0) {
      constexpr auto targetField =
          ThroughField(&ThroughModel::targetPk, TargetModel::pkFieldPtr(), ThroughPtrType::TARGET);
      return std::tuple{targetField};
    } else {
      static constexpr auto result = []<std::size_t... I>(std::index_sequence<I...>) {
        return std::tuple_cat(getThroughFieldIfTarget<I, Relation>()...);
      }(std::make_index_sequence<std::tuple_size_v<decltype(Relation::throughFields)>>{});
      return result;
    }
  }

  template <typename Relation> static constexpr auto getManyToManyRelationThroughFields() {
    using ThroughModel = Relation::Through;
    using DefinerModel = Relation::Definer;
    using TargetModel = Relation::Target;
    if constexpr (Relation::throughFieldCount == 0) {
      constexpr auto throughField1 =
          ThroughField(&ThroughModel::targetPk, TargetModel::pkFieldPtr(), ThroughPtrType::TARGET);
      constexpr auto throughField2 =
          ThroughField(&ThroughModel::definerPk, DefinerModel::pkFieldPtr(), ThroughPtrType::DEFINER);
      return std::tuple{throughField1, throughField2};
    } else {
      return Relation::throughFields;
    }
  }

  template <std::size_t I, typename Relation> static constexpr auto getThroughFieldIfDefiner() {
    constexpr auto throughFields = Relation::throughFields;
    constexpr auto currentField = std::get<I>(throughFields);

    if constexpr (currentField.throughPtrType == ThroughPtrType::DEFINER)
      return std::tuple{currentField};
    else
      return std::tuple{};
  }

  template <std::size_t I, typename Relation> static constexpr auto getThroughFieldIfTarget() {
    constexpr auto throughFields = Relation::throughFields;
    constexpr auto currentField = std::get<I>(throughFields);

    if constexpr (currentField.throughPtrType == ThroughPtrType::TARGET)
      return std::tuple{currentField};
    else
      return std::tuple{};
  }

  template <typename LookupModel, std::size_t I> static constexpr auto getManyToManyRelationIfTargetModel() {
    using RelT = std::remove_cvref_t<decltype(std::get<I>(manyToManyRelations()))>;
    if constexpr (std::same_as<typename RelT::Target, LookupModel>)
      return std::tuple{std::get<I>(manyToManyRelations())};
    else
      return std::tuple{};
  }

  template <FixedString Name, std::size_t I> static constexpr auto getManyToManyRelationIfNamed() {
    if constexpr (std::get<I>(manyToManyRelations()).relationName == Name.view())
      return std::tuple{std::get<I>(manyToManyRelations())};
    else
      return std::tuple{};
  }

  template <std::size_t I> static constexpr auto getRelationIfManyToMany() {
    using RelationsTuple = decltype(Model::relations());
    using RelT = std::tuple_element_t<I, RelationsTuple>;

    if constexpr (RelT::relationType == RelationType::MANY_TO_MANY)
      return std::tuple{std::get<I>(Model::relations())};
    else
      return std::tuple<>{};
  }

  template <typename Relation, typename ThroughModel>
  static ThroughModel getMirrorThroughObject(const ThroughModel &throughObj) {
    ThroughModel mirrorThroughObj = throughObj;
    constexpr auto definerFields = getManyToManyRelationDefinerThroughFields<Relation>();
    constexpr auto targetFields = getManyToManyRelationTargetThroughFields<Relation>();

    [&]<std::size_t... I>(std::index_sequence<I...>) {
      ((mirrorThroughObj.*(std::get<I>(definerFields).throughPtr) = throughObj.*(std::get<I>(targetFields).throughPtr),
        mirrorThroughObj.*(std::get<I>(targetFields).throughPtr) = throughObj.*(std::get<I>(definerFields).throughPtr)),
       ...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(targetFields)>>{});
    return mirrorThroughObj;
  }

  //==============================ONE TO ONE/ MANY TO ONE==============================

  template <typename LookupModel, std::size_t I> static constexpr auto getFkRelationIfTargetModel() {
    using RelT = std::remove_cvref_t<decltype(std::get<I>(fkRelations()))>;
    if constexpr (std::same_as<typename RelT::Target, LookupModel>)
      return std::tuple{std::get<I>(fkRelations())};
    else
      return std::tuple{};
  }

  template <FixedString Name, std::size_t I> static constexpr auto getFkRelationIfNamed() {
    if constexpr (std::get<I>(fkRelations()).relatedName == Name.view())
      return std::tuple{std::get<I>(fkRelations())};
    else
      return std::tuple{};
  }

  template <std::size_t I> static constexpr auto getRelationIfFk() {
    using RelationsTuple = decltype(Model::relations());
    using RelT = std::tuple_element_t<I, RelationsTuple>;

    if constexpr (RelT::relationType == RelationType::ONE_TO_ONE or RelT::relationType == RelationType::MANY_TO_ONE)
      return std::tuple{std::get<I>(Model::relations())};
    else
      return std::tuple<>{};
  }
};
} // namespace rukh::orm
