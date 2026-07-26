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

  static std::optional<Model> find(pkType pkVal) {
    Column<pkType, Model> pkColumn = Model::pkColumn();
    return SelectQuery<Model>().where({pkColumn.name, "=", pkVal}).first(Model::db);
  }

  static SelectQuery<Model> all() { return SelectQuery<Model>(); }
  static SelectQuery<Model> filter(Predicate p) { return SelectQuery<Model>().where(p); }

  bool save() {
    if (persisted_)
      return update();
    else
      return insert();
  }

  bool insert() {
    Model *self = static_cast<Model *>(this);
    auto [rowsAffected, objs] = InsertQuery<Model>().execute(Model::db, {*self});
    setPersisted();
    return rowsAffected > 0;
  }

  bool update() {
    auto pkColumn = Model::pkColumn();
    Model *self = static_cast<Model *>(this);
    Predicate p(pkColumn.name, "=", self->*pkColumn.fieldPtr);
    auto [rowsAffected, _] = UpdateQuery<Model>().where(p).execute(Model::db, *self);
    return rowsAffected > 0;
  }

  bool destroy() {
    auto pkColumn = Model::pkColumn();
    Model *self = static_cast<Model *>(this);
    Predicate p("id", "=", self->*pkColumn.fieldPtr);
    auto [rowsAffected, _] = DeleteQuery<Model>().where(p).execute(Model::db);
    return rowsAffected > 0;
  }

  void setPersisted() { persisted_ = true; }

private:
  bool persisted_ = false;
};
} // namespace rukh::orm
