#pragma once

#include <optional>

#include <rukh/orm/Predicate.hpp>

namespace rukh::orm {

template <typename Derived> class WhereClause {
public:
  Derived &where(Predicate p) {
    whereChanged = true;
    if (not wherePredicate.has_value())
      wherePredicate = p;
    else
      wherePredicate = *wherePredicate && p;
    return static_cast<Derived &>(*this);
  }

  Derived &andWhere(Predicate p) {
    whereChanged = true;
    return where(p);
  }

  Derived &orWhere(Predicate p) {
    whereChanged = true;
    if (not wherePredicate.has_value())
      wherePredicate = p;
    else
      wherePredicate = *wherePredicate || p;
    return static_cast<Derived &>(*this);
  }

protected:
  bool whereChanged = true;
  std::optional<Predicate> wherePredicate = std::nullopt;
};

} // namespace rukh::orm
