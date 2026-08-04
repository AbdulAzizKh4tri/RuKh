#pragma once

#include <optional>

#include <rukh/orm/Predicate.hpp>

namespace rukh::orm {

template <typename Derived, typename... Models> class WhereClause {
public:
  Derived &where(const Predicate<Models...> p) {
    if (not wherePredicate.has_value())
      wherePredicate = p;
    else
      wherePredicate = *wherePredicate && p;
    return static_cast<Derived &>(*this);
  }

  Derived &andWhere(const Predicate<Models...> &p) { return where(p); }

  Derived &orWhere(const Predicate<Models...> p) {
    if (not wherePredicate.has_value())
      wherePredicate = p;
    else
      wherePredicate = *wherePredicate || p;
    return static_cast<Derived &>(*this);
  }

protected:
  std::optional<Predicate<Models...>> wherePredicate = std::nullopt;
};

} // namespace rukh::orm
