#pragma once

#include <string>
#include <string_view>
#include <tuple>

#include <rukh/TypeHelpers.hpp>

namespace rukh::orm {

template <typename... FieldPtrs> struct UniqueTogetherConstraint {
  using FieldPtrsTuple = std::tuple<FieldPtrs...>;
  const FieldPtrsTuple fieldPtrs;
  std::string constraintName = "";

  UniqueTogetherConstraint &withConstraintName(const std::string_view name) {
    constraintName = name;
    return *this;
  }
};

template <typename... FieldPtrs> inline auto makeUniqueTogether(FieldPtrs... fieldPtrs) {
  return UniqueTogetherConstraint<FieldPtrs...>{std::make_tuple(fieldPtrs...)};
}

} // namespace rukh::orm
