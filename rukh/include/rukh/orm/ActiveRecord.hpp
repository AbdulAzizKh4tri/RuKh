#pragma once

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
    co_return co_await SelectQuery<Model>().where({pkColumn.name, "=", pkVal}).first(Model::db);
  }

  static SelectQuery<Model> all() { return SelectQuery<Model>(); }
  static SelectQuery<Model> filter(Predicate p) { return SelectQuery<Model>().where(p); }

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
    auto [rowsAffected, objs] = co_await query.execute(Model::db, inputObjs, true);
    if (rowsAffected > 0)
      self->id = objs[0].id;
    setPersisted();
    co_return rowsAffected > 0;
  }

  Task<bool> update() {
    auto pkColumn = Model::pkColumn();
    Model *self = static_cast<Model *>(this);
    Predicate p(pkColumn.name, "=", self->*pkColumn.fieldPtr);
    auto [rowsAffected, _] = co_await UpdateQuery<Model>().where(p).execute(Model::db, *self);
    co_return rowsAffected > 0;
  }

  Task<bool> destroy() {
    auto pkColumn = Model::pkColumn();
    Model *self = static_cast<Model *>(this);
    Predicate p(pkColumn.name, "=", self->*pkColumn.fieldPtr);
    auto [rowsAffected, _] = co_await DeleteQuery<Model>().where(p).execute(Model::db);
    co_return rowsAffected > 0;
  }

  void setPersisted() { persisted_ = true; }

private:
  bool persisted_ = false;
};
} // namespace rukh::orm
