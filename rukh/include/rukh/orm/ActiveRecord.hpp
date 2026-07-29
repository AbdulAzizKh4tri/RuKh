#pragma once

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>

#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/orm/Column.hpp>
#include <rukh/orm/Predicate.hpp>

#include <rukh/orm/DeleteQuery.hpp>
#include <rukh/orm/InsertQuery.hpp>
#include <rukh/orm/SelectQuery.hpp>
#include <rukh/orm/UpdateQuery.hpp>

namespace rukh::orm {
template <typename T> struct remove_optional {
  using type = T;
};
template <typename T> struct remove_optional<std::optional<T>> {
  using type = T;
};
template <typename T> using remove_optional_t = typename remove_optional<T>::type;

template <typename Model, auto MemPtr> using field_t = std::remove_cvref_t<decltype(std::declval<Model>().*MemPtr)>;
template <typename Model, auto MemPtr> using raw_field_t = remove_optional_t<field_t<Model, MemPtr>>;

template <typename Model, typename... PkTypes> class ActiveRecord {
public:
  static constexpr std::size_t pkArity = sizeof...(PkTypes);
  using PkType = std::tuple<PkTypes...>;

  static Task<std::expected<std::optional<Model>, db::DatabaseError>> find(const PkType &pkVal,
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

  static Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  bulkUpdate(const Model &newObj, const std::vector<std::string> &columns, const Predicate<Model> &p,
             db::ITransaction *transaction = nullptr) {
    UpdateQuery<Model> query;
    for (auto &col : columns)
      query.column(col);
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

  static constexpr bool isValidColumnName(const std::string &name) {
    return std::apply([&](auto &&...col) { return ((col.dbName == name) or ...); }, Model::columns());
  }

  static constexpr auto pkColumns() {
    static constexpr auto result = []<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple_cat(slot<I>()...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(Model::columns())>>{});
    return result;
  }

  static constexpr bool isPkColumn(const std::string &name) {
    auto cols = pkColumns();
    return std::apply([&](auto &&...col) { return ((col.dbName == name) or ...); }, cols);
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
    Predicate p = buildPkPredicate(this->getPrimaryKey());
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
    Predicate p = buildPkPredicate(this->getPrimaryKey());
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

  PkType getPrimaryKey() const {
    auto cols = pkColumns();
    const Model *self = static_cast<const Model *>(this);
    return [&, this]<std::size_t... I>(std::index_sequence<I...>) {
      return PkType{(self->*std::get<I>(cols).fieldPtr)...};
    }(std::make_index_sequence<pkArity>{});
  }

  void setPersisted() { persisted_ = true; }
  void resetPersisted() { persisted_ = false; }

private:
  bool persisted_ = false;

  //==============================HELPERS==============================
  template <std::size_t I> static constexpr auto slot() {
    if constexpr (std::get<I>(Model::columns()).isPrimaryKey)
      return std::tuple{std::get<I>(Model::columns())};
    else
      return std::tuple<>{};
  }

  static Predicate<Model> buildPkPredicate(const PkType &pkVal) {
    auto cols = Model::pkColumns();
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return (Predicate<Model>::equals(std::get<I>(cols).fieldPtr, db::toDbValue(std::get<I>(pkVal))) && ...);
    }(std::make_index_sequence<pkArity>{});
  }
};
} // namespace rukh::orm
