#pragma once

#include <cstddef>
#include <string>
#include <tuple>

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>

#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/orm/Column.hpp>
#include <rukh/orm/Predicate.hpp>

#include <rukh/TypeHelpers.hpp>
#include <rukh/orm/DeleteQuery.hpp>
#include <rukh/orm/InsertQuery.hpp>
#include <rukh/orm/Relations.hpp>
#include <rukh/orm/SelectQuery.hpp>
#include <rukh/orm/UpdateQuery.hpp>
#include <type_traits>
#include <utility>

namespace rukh::orm {

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

  Task<std::expected<Model, db::DatabaseError>> insert(db::ITransaction *transaction = nullptr) {
    Model *self = static_cast<Model *>(this);
    InsertQuery<Model> query;
    std::vector<Model> inputObjs{*self};

    auto result = co_await query.execute(inputObjs, transaction, true);
    if (not result)
      co_return std::unexpected(result.error());

    auto [_, objs] = *result;
    if (objs.empty())
      throw DatabaseException("Failed to insert object");

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

    if (objs.empty())
      throw DatabaseException("Failed to update object");

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

  // TODO: symmetric
  template <typename RelatedModel, FixedString RelationName = "">
  auto manyRelated(db::ITransaction *transaction = nullptr) {
    static constexpr auto relation = [] {
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
      if constexpr (relSize == 1) {
        return std::get<0>(rels);
      } else {
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
        static_assert(relSizeOther > 0, "No many-to-many relation found with this model combination");

        return std::get<0>(relsOther);
      }
    }();

    using Relation = decltype(relation);
    using ThroughModel = Relation::Through;
    using DefinerModel = Relation::Definer;
    using TargetModel = Relation::Target;

    constexpr size_t throughFieldCount = Relation::throughFieldCount;
    constexpr bool noThroughFields = throughFieldCount == 0;
    constexpr auto throughFields = [&] {
      if constexpr (noThroughFields) {
        constexpr auto throughField1 =
            ThroughField(&ThroughModel::targetPk, TargetModel::pkFieldPtr(), ThroughPtrType::TARGET);
        constexpr auto throughField2 =
            ThroughField(&ThroughModel::definerPk, DefinerModel::pkFieldPtr(), ThroughPtrType::DEFINER);
        return std::tuple{throughField1, throughField2};
      } else {
        return Relation::throughFields;
      }
    }();

    using queryModels = std::tuple<RelatedModel, Model, ThroughModel>;
    using Query = unpack_tuple_t<SelectQuery, queryModels>;
    using Pred = unpack_tuple_t<Predicate, queryModels>;

    constexpr bool thisIsDefiner = std::is_same_v<DefinerModel, Model>;

    const std::string relatedAlias = "r";
    const std::string callerAlias = "c";
    const std::string throughAlias = "t";
    Query query(relatedAlias);

    Pred rJoinPredicate(true);
    std::apply(
        [&](auto &&...throughflds) {
          auto addPredicate = [&](auto &&throughfld) {
            if ((thisIsDefiner and throughfld.throughPtrType == ThroughPtrType::TARGET) or
                (not thisIsDefiner and throughfld.throughPtrType == ThroughPtrType::DEFINER))
              rJoinPredicate = rJoinPredicate and
                               Pred::equals(throughfld.throughPtr, throughfld.modelPtr, {throughAlias, relatedAlias});
          };
          (addPredicate(throughflds), ...);
        },
        throughFields);

    Pred cJoinPredicate(true);
    std::apply(
        [&](auto &&...throughflds) {
          auto addPredicate = [&](auto &&throughfld) {
            if ((thisIsDefiner and throughfld.throughPtrType == ThroughPtrType::DEFINER) or
                (not thisIsDefiner and throughfld.throughPtrType == ThroughPtrType::TARGET))
              cJoinPredicate = cJoinPredicate and
                               Pred::equals(throughfld.throughPtr, throughfld.modelPtr, {throughAlias, callerAlias});
          };
          (addPredicate(throughflds), ...);
        },
        throughFields);

    query.template join<ThroughModel>(rJoinPredicate, throughAlias).template join<Model>(cJoinPredicate, callerAlias);

    const Model *self = static_cast<const Model *>(this);

    Pred wherePred(true);
    std::apply(
        [&](auto &&...throughflds) {
          auto addPredicate = [&](auto &&throughfld) {
            using ThroughFieldModelPtr = get_class_t<std::remove_cvref_t<decltype(throughfld.modelPtr)>>;
            if constexpr (std::is_same_v<ThroughFieldModelPtr, Model>) {
              if ((thisIsDefiner and throughfld.throughPtrType == ThroughPtrType::DEFINER) or
                  (!thisIsDefiner and throughfld.throughPtrType == ThroughPtrType::TARGET))
                wherePred = wherePred and Pred::equals(throughfld.modelPtr, self->*(throughfld.modelPtr), callerAlias);
            }
          };
          (addPredicate(throughflds), ...);
        },
        throughFields);

    query.where(wherePred);

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
    if constexpr (std::get<I>(Model::columns()).isPrimaryKey)
      return std::tuple{std::get<I>(Model::columns())};
    else
      return std::tuple<>{};
  }

  template <std::size_t I> static constexpr auto getFieldPtrIfPk() {
    if constexpr (std::get<I>(Model::columns()).isPrimaryKey)
      return std::tuple{std::get<I>(Model::columns()).fieldPtr};
    else
      return std::tuple<>{};
  }

  static Predicate<Model> buildPkPredicate(const PkTypesTuple &pkVal) {
    auto cols = pkColumns();
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return (Predicate<Model>::equals(std::get<I>(cols).fieldPtr, std::get<I>(pkVal)) && ...);
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
