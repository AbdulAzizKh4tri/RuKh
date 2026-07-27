#pragma once

#include <unordered_set>

#include <rukh/Task.hpp>
#include <rukh/db/IDatabase.hpp>

#include <rukh/orm/Column.hpp>
#include <rukh/orm/Predicate.hpp>

#include <rukh/orm/DeleteQuery.hpp>
#include <rukh/orm/InsertQuery.hpp>
#include <rukh/orm/SelectQuery.hpp>
#include <rukh/orm/UpdateQuery.hpp>

namespace rukh::orm {

template <typename Model, typename pkType> class ActiveRecord {
public:
  using pk = pkType;

  static Task<std::optional<Model>> find(pkType pkVal) {
    Column<pkType, Model> pkColumn = Model::pkColumn();
    co_return co_await SelectQuery<Model>().where(Predicate<Model>::equals(pkColumn.fieldPtr, pkVal)).first();
  }

  static SelectQuery<Model> all() { return SelectQuery<Model>(); }
  static SelectQuery<Model> filter(const Predicate<Model> &p) { return SelectQuery<Model>().where(p); }

  static Task<std::pair<size_t, std::vector<Model>>> bulkInsert(const std::vector<Model> &objs) {
    InsertQuery<Model> query;
    co_return co_await query.execute(objs, true);
  }

  static Task<std::pair<size_t, std::vector<Model>>>
  bulkUpdate(const Model &newObj, const std::vector<std::string> &columns, const Predicate<Model> &p) {
    UpdateQuery<Model> query;
    for (auto &col : columns)
      query.column(col);
    co_return co_await query.where(p).execute(newObj, true);
  }

  static Task<std::pair<size_t, std::vector<Model>>> bulkDestroy(const Predicate<Model> &p) {
    DeleteQuery<Model> query;
    co_return co_await query.where(p).execute(true);
  }

  Task<bool> save() {
    if (persisted_)
      co_return co_await update();
    else
      co_return co_await insert();
  }

  Task<bool> insert() {
    Model *self = static_cast<Model *>(this);
    InsertQuery<Model> query;
    std::vector<Model> inputObjs{*self};
    auto [rowsAffected, objs] = co_await query.execute(inputObjs, true);
    if (rowsAffected > 0)
      self->id = objs[0].id;
    setPersisted();
    co_return rowsAffected > 0;
  }

  Task<bool> update() {
    auto pkColumn = Model::pkColumn();
    Model *self = static_cast<Model *>(this);
    Predicate p(pkColumn.fieldPtr, Operator::EQUALS, self->*pkColumn.fieldPtr);
    auto [rowsAffected, _] = co_await UpdateQuery<Model>().where(p).execute(*self);
    co_return rowsAffected > 0;
  }

  Task<bool> destroy() {
    auto pkColumn = Model::pkColumn();
    Model *self = static_cast<Model *>(this);
    Predicate p(pkColumn.fieldPtr, Operator::EQUALS, self->*pkColumn.fieldPtr);
    auto [rowsAffected, _] = co_await DeleteQuery<Model>().where(p).execute();
    co_return rowsAffected > 0;
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
                result = c.name;
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

  static constexpr std::unordered_set<std::string> &validColumnNames() {
    static std::unordered_set<std::string> names = [] {
      std::unordered_set<std::string> s;
      std::apply([&](auto &&...col) { (s.insert(col.name), ...); }, Model::columns());
      return s;
    }();
    return names;
  }

  static constexpr bool isValidColumnName(const std::string &name) {
    return validColumnNames().find(name) != validColumnNames().end();
  }

  void setPersisted() { persisted_ = true; }

private:
  bool persisted_ = false;
};
} // namespace rukh::orm
