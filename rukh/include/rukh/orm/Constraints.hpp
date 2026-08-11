#pragma once

#include <string>
#include <string_view>
#include <tuple>

#include <rukh/TypeHelpers.hpp>
#include <rukh/orm/Predicate.hpp>

namespace rukh::orm {

template <typename Model, typename... FieldT> struct UniqueTogetherConstraint {
  using FieldPtrsTuple = std::tuple<FieldT Model::*...>;
  const FieldPtrsTuple fieldPtrs;
  std::string constraintName = "";

  UniqueTogetherConstraint &withConstraintName(const std::string_view name) {
    constraintName = name;
    return *this;
  }
};

template <typename Model, typename... FieldT> inline constexpr auto makeUniqueTogether(FieldT Model::*...fieldPtrs) {
  return UniqueTogetherConstraint<Model, FieldT...>{std::make_tuple(fieldPtrs...)};
}

struct CheckColumnResolver {
  template <typename... Models, typename FieldPtr> static std::string resolve(FieldPtr fieldPtr, const std::string &) {

    using FieldPtrModel = get_class_t<FieldPtr>;
    return FieldPtrModel::columnNameOf(fieldPtr);
  }
};

template <typename Model> using CheckPredicate = BasePredicate<CheckColumnResolver, Model>;

template <typename Model> struct CheckConstraint {
  CheckPredicate<Model> predicate;
};

template <typename Model> inline auto constexpr checkConstraint(const CheckPredicate<Model> &predicate) {
  return CheckConstraint<Model>{predicate};
}

} // namespace rukh::orm
