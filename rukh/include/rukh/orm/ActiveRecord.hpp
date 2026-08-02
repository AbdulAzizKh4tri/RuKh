#pragma once

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

namespace rukh::orm {

template <typename Model, typename... PkTypes> class ActiveRecord {

public:
  static constexpr std::size_t pkArity = sizeof...(PkTypes);
  using PkTypesTuple = std::tuple<PkTypes...>;
  using PkType = std::tuple_element_t<0, PkTypesTuple>;

  operator PkType() const { return getSinglePrimaryKeyValue(); }

  static Task<std::expected<std::optional<Model>, db::DatabaseError>> find(const PkTypesTuple &pkVal,
                                                                           db::ITransaction *transaction = nullptr) {
    co_return co_await SelectQuery<Model>().where(buildPkPredicate(pkVal)).first(transaction);
  }

  static Task<std::expected<Model, db::DatabaseError>> findOrCreate(Model obj,
                                                                    db::ITransaction *transaction = nullptr) {
    auto objPk = obj.getPrimaryKey();
    auto findResult = co_await find(objPk, transaction);
    if (not findResult)
      co_return std::unexpected(findResult.error());
    if (auto userOpt = *findResult; userOpt) {
      co_return *userOpt;
    }
    auto insertResult = co_await InsertQuery<Model>().execute({obj}, transaction, true);
    if (not insertResult) {
      if (insertResult.error().type == db::DbErrorType::DUPLICATE_KEY) {
        auto refetch = co_await find(objPk, transaction);
        if (not refetch)
          throw DatabaseException("Huh? Insert returned a duplicate key, but could not find the object");
        co_return *refetch;
      } else {
        co_return std::unexpected(insertResult.error());
      }
    }
    auto [_, objs] = *insertResult;
    co_return objs[0];
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

  Task<std::expected<Model, db::DatabaseError>> save(db::ITransaction *transaction = nullptr) {
    // if called inside a transaction that is later rolled back, this object's persisted_/id will not reflect that — do
    // not reuse a model object after a rollback without re-fetching it.

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
    Predicate p = buildPkPredicate(this->getPrimaryKeyValues());
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
    Predicate p = buildPkPredicate(this->getPrimaryKeyValues());
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

  PkType getSinglePrimaryKeyValue() const {
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

  static constexpr bool isPkColumn(const std::string &name) {
    auto cols = pkColumns();
    return std::apply([&](auto &&...col) { return ((col.dbName == name) or ...); }, cols);
  }

  static constexpr auto oneToOneRelations() {
    constexpr auto rels = Model::relations();
    static constexpr auto result = []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getRelationIfOneToOne<I>(rels)...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(rels)>>{});
    return result;
  }

  static constexpr auto manyToOneRelations() {
    constexpr auto rels = Model::relations();
    static constexpr auto result = []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(getRelationIfManyToOne<I>(rels)...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(rels)>>{});
    return result;
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

  std::string toString(const int indent = -1) const { return toJson().dump(indent); }

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

  //==============================HELPERS==============================
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
  //==============================Relation Helpers==============================
  //==============================ONE TO ONE==============================
  template <typename T> struct is_one_to_one_relation : std::false_type {};
  template <typename ModelA, typename ModelB, typename FkFieldPtrsTuple>
  struct is_one_to_one_relation<OneToOneRelation<ModelA, ModelB, FkFieldPtrsTuple>> : std::true_type {};
  template <typename T> static constexpr bool is_one_to_one_relation_v = is_one_to_one_relation<T>::value;

  template <std::size_t I> static constexpr auto getRelationIfOneToOne() {
    using RelationsTuple = decltype(Model::relations());
    using RelT = std::tuple_element_t<I, RelationsTuple>;

    if constexpr (is_one_to_one_relation_v<RelT>) {
      return std::tuple{std::get<I>(Model::relations())};
    } else {
      return std::tuple<>{};
    }
  }
  //==============================Many TO ONE==============================
  template <typename T> struct is_many_to_one_relation : std::false_type {};
  template <typename ModelA, typename ModelB, typename FkFieldPtrsTuple>
  struct is_many_to_one_relation<ManyToOneRelation<ModelA, ModelB, FkFieldPtrsTuple>> : std::true_type {};
  template <typename T> static constexpr bool is_many_to_one_relation_v = is_many_to_one_relation<T>::value;

  template <std::size_t I> static constexpr auto getRelationIfManyToOne() {
    using RelationsTuple = decltype(Model::relations());
    using RelT = std::tuple_element_t<I, RelationsTuple>;

    if constexpr (is_many_to_one_relation_v<RelT>) {
      return std::tuple{std::get<I>(Model::relations())};
    } else {
      return std::tuple<>{};
    }
  }

  static Predicate<Model> buildPkPredicate(const PkTypesTuple &pkVal) {
    auto cols = Model::pkColumns();
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return (Predicate<Model>::equals(std::get<I>(cols).fieldPtr, std::get<I>(pkVal)) && ...);
    }(std::make_index_sequence<pkArity>{});
  }
};
} // namespace rukh::orm
