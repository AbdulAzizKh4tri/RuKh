#pragma once

#include <optional>

#include <rukh/orm/Predicate.hpp>

namespace rukh::orm {

template <typename Model, typename Derived> class WhereClause {
public:
  Derived &where(const Predicate<Model> &p) {
    if (not wherePredicate.has_value())
      wherePredicate = p;
    else
      wherePredicate = *wherePredicate && p;
    return static_cast<Derived &>(*this);
  }

  Derived &andWhere(const Predicate<Model> &p) { return where(p); }

  Derived &orWhere(const Predicate<Model> &p) {
    if (not wherePredicate.has_value())
      wherePredicate = p;
    else
      wherePredicate = *wherePredicate || p;
    return static_cast<Derived &>(*this);
  }

protected:
  std::optional<Predicate<Model>> wherePredicate = std::nullopt;
};

} // namespace rukh::orm
