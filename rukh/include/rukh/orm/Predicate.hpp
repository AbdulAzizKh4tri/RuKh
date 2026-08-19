#pragma once

#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/DbValue.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Column.hpp>

namespace rukh::orm {
template <typename... Models> class SelectQuery;

static constexpr std::string_view aliasList[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
                                                 "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"};
static constexpr std::string getAlias(std::size_t index) { return std::string(aliasList[index]); }

enum class Operator {
  EQUALS,
  NOT_EQUALS,
  GREATER,
  LESSER,
  GREATER_OR_EQUAL,
  LESSER_OR_EQUAL,
  LIKE,
  IN,
  NOT_IN,
  IS_NULL,
  IS_NOT_NULL,
  BETWEEN
};

template <typename ColumnResolver, typename... Models> struct BasePredicate {
  using ModelsTuple = std::tuple<Models...>;

  static constexpr std::string_view opStrings[] = {
      "=", "!=", ">", "<", ">=", "<=", "LIKE", "IN", "NOT IN", "IS NULL", "IS NOT NULL", "BETWEEN"};
  static constexpr std::string_view to_string(Operator op) { return opStrings[static_cast<std::size_t>(op)]; }

  constexpr bool isGroupOperator(Operator op) const {
    switch (op) {
    case Operator::IN:
    case Operator::NOT_IN:
    case Operator::BETWEEN:
      return true;
    default:
      return false;
    }
  }

  enum class PredicateType {
    LEAF,
    AND,
    OR,
    TRUE,
    FALSE,
    STRING,
    FIELD_COMPARISON,
    SUBQUERY,
    EXISTS,
    NOT_EXISTS,
    RAW
  } predicateType;

  std::string columnA;
  std::string columnB;

  Operator op;                     // only used if
  std::vector<db::DbValue> values; // Kind::Leaf

  std::vector<BasePredicate> children;

  std::string lhsFunction;
  std::string rhsFunction;

  //===============CONSTRUCTORS===============

  // custom string escape hatches
  BasePredicate(const std::string &str, const std::vector<db::DbValue> &vals)
      : predicateType(PredicateType::STRING), columnA(str) {
    values.insert(values.end(), vals.begin(), vals.end());
  }

  BasePredicate(const std::string &col, Operator opr, const std::vector<db::DbValue> &vals)
      : predicateType(PredicateType::LEAF), op(opr), columnA(col) {
    values.insert(values.end(), vals.begin(), vals.end());
  }

  BasePredicate(const std::string &lhs, Operator opr, const std::string &rhs, const std::vector<db::DbValue> &vals = {})
      : predicateType(PredicateType::RAW), op(opr), columnA(lhs), columnB(rhs) {
    values.insert(values.end(), vals.begin(), vals.end());
  }

  // bool preds
  BasePredicate(bool val) {
    if (val)
      predicateType = PredicateType::TRUE;
    else
      predicateType = PredicateType::FALSE;
  }

  // field to values
  template <FieldPointer FieldPtr>
  BasePredicate(FieldPtr fieldPtr, Operator opr, const std::vector<get_raw_field_t<FieldPtr>> &vals,
                const std::string &tableAlias)
      : predicateType(PredicateType::LEAF), columnA(getColumnWithAlias(fieldPtr, tableAlias)), op(opr) {
    values.insert(values.end(), vals.begin(), vals.end());
  }

  // field to values with functions
  template <FieldPointer FieldPtr>
  BasePredicate(FieldPtr fieldPtr, Operator opr, const std::vector<get_raw_field_t<FieldPtr>> &vals,
                const std::string &lhsFunc, const std::string &rhsFunc, const std::string &tableAlias)
      : BasePredicate(fieldPtr, opr, vals, tableAlias) {
    lhsFunction = lhsFunc;
    rhsFunction = rhsFunc;
  }

  // field to Subquery
  template <FieldPointer FieldPtr>
  BasePredicate(FieldPtr fieldPtr, Operator opr, SelectQuery<Models...> &subQuery, const std::string &tableAlias)
      : predicateType(PredicateType::SUBQUERY), columnA(getColumnWithAlias(fieldPtr, tableAlias)), op(opr) {
    auto sqlAndParams = subQuery.getSqlAndParams();
    columnB = sqlAndParams.first;
    values.insert(values.end(), sqlAndParams.second.begin(), sqlAndParams.second.end());
  }

  // field to Subquery with functions
  template <FieldPointer FieldPtr>
  BasePredicate(FieldPtr fieldPtr, Operator opr, SelectQuery<Models...> &subQuery, const std::string &lhsFunc,
                const std::string &tableAlias)
      : BasePredicate(fieldPtr, opr, subQuery, tableAlias) {
    lhsFunction = lhsFunc;
  }

  // Exists Subquery
  BasePredicate(SelectQuery<Models...> &subQuery, bool exists)
      : predicateType(exists ? PredicateType::EXISTS : PredicateType::NOT_EXISTS) {
    auto [sql, params] = subQuery.getSqlAndParams();
    columnA = sql;
    values.insert(values.end(), params.begin(), params.end());
  }

  // field to field
  template <typename FieldTA, typename FieldTB, typename ModelA, typename ModelB>
  BasePredicate(FieldTA ModelA::*fieldPtrA, Operator opr, FieldTB ModelB::*fieldPtrB,
                const std::pair<std::string, std::string> tableAliases)
      : op(opr), predicateType(PredicateType::FIELD_COMPARISON),
        columnA(getColumnWithAlias(fieldPtrA, tableAliases.first)),
        columnB(getColumnWithAlias(fieldPtrB, tableAliases.second)) {}

  // field to field with functions
  template <typename FieldTA, typename FieldTB, typename ModelA, typename ModelB>
  BasePredicate(FieldTA ModelA::*fieldPtrA, Operator opr, FieldTB ModelB::*fieldPtrB, const std::string &lhsFunc,
                const std::string &rhsFunc, const std::pair<std::string, std::string> tableAliases)
      : BasePredicate(fieldPtrA, opr, fieldPtrB, tableAliases) {
    lhsFunction = lhsFunc;
    rhsFunction = rhsFunc;
  }

  // Special case for between with functions
  template <typename FieldTA, typename FieldTB, typename FieldTC, typename ModelA, typename ModelB, typename ModelC>
  BasePredicate(FieldTA ModelA::*fieldPtrA, Operator opr, FieldTB ModelB::*fieldPtrB, FieldTC ModelC::*fieldPtrC,
                const std::string &lhsFunc, const std::string &rhsFunc,
                const std::tuple<std::string, std::string, std::string> tableAliases)
      : op(opr), predicateType(PredicateType::LEAF),
        columnA(lhsFunc + "(" + getColumnWithAlias(fieldPtrA, std::get<0>(tableAliases)) + ")") {

    columnB = rhsFunc + "(" + getColumnWithAlias(fieldPtrB, std::get<1>(tableAliases)) + ") AND " + rhsFunc + "(" +
              getColumnWithAlias(fieldPtrC, std::get<2>(tableAliases)) + ")";
  }

  BasePredicate(PredicateType p) : predicateType(p) {}

  //===============OPERATORS===============

  BasePredicate operator||(const BasePredicate &rhs) const {
    if (this->predicateType == PredicateType::TRUE || rhs.predicateType == PredicateType::TRUE)
      return BasePredicate(true);

    if (this->predicateType == PredicateType::FALSE)
      return rhs;

    if (rhs.predicateType == PredicateType::FALSE)
      return *this;

    BasePredicate p(PredicateType::OR);
    p.children.push_back(*this);
    p.children.push_back(rhs);
    return p;
  }

  BasePredicate operator&&(const BasePredicate &rhs) const {
    if (this->predicateType == PredicateType::FALSE || rhs.predicateType == PredicateType::FALSE)
      return BasePredicate(false);

    if (this->predicateType == PredicateType::TRUE)
      return rhs;

    if (rhs.predicateType == PredicateType::TRUE)
      return *this;

    BasePredicate p(PredicateType::AND);
    p.children.push_back(*this);
    p.children.push_back(rhs);
    return p;
  }

  //===============HELPERS===============

  //===============EXISTS===============
  //
  static BasePredicate<ColumnResolver, Models...> exists(SelectQuery<Models...> &subQuery) { // field - val
    return BasePredicate<ColumnResolver, Models...>(subQuery, true);
  }

  static BasePredicate<ColumnResolver, Models...> notExists(SelectQuery<Models...> &subQuery) { // field - val
    return BasePredicate<ColumnResolver, Models...>(subQuery, false);
  }

  //===============NULL===============
  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> isNull(FieldT Model::*fieldPtr,
                                                         const std::string &tableAlias = "") { // field - val
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "equals(): FieldPtr from a model not specified in BasePredicate<...>");
    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::IS_NULL, {}, tableAlias);
  }

  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> isNotNull(FieldT Model::*fieldPtr,
                                                            const std::string &tableAlias = "") { // field - val
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "equals(): FieldPtr from a model not specified in BasePredicate<...>");
    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::IS_NOT_NULL, {}, tableAlias);
  }

  //===============EQUALS===============
  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> equals(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val,
                                                         const std::string &tableAlias = "") { // field - val
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "equals(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::EQUALS, {val}, tableAlias);
  }

  template <typename FieldTA, typename FieldTB, typename ModelA, typename ModelB>
  static BasePredicate<ColumnResolver, Models...>
  equals(FieldTA ModelA::*fieldPtrA, FieldTB ModelB::*fieldPtrB,
         const std::pair<std::string, std::string> tableAliases = {}) { // field-field
    static_assert(is_in_tuple_v<ModelA, ModelsTuple>,
                  "equals(): FieldPtr from a model not specified in BasePredicate<...>");
    static_assert(is_in_tuple_v<ModelB, ModelsTuple>,
                  "equals(): FieldPtr from a model not specified in BasePredicate<...>");
    return BasePredicate<ColumnResolver, Models...>(fieldPtrA, Operator::EQUALS, fieldPtrB, tableAliases);
  }

  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> equals(FieldT Model::*fieldPtr, SelectQuery<Models...> &subQuery,
                                                         const std::string &tableAlias = "") { // field-subquery
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "equals(): FieldPtr from a model not specified in BasePredicate<...>");
    validateScalarSubQuery(subQuery, "EQUALS");
    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::EQUALS, subQuery, tableAlias);
  }

  //===============IN===============
  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> in(FieldT Model::*fieldPtr,
                                                     const std::vector<remove_optional_t<FieldT>> &vals,
                                                     const std::string &tableAlias = "") { // field-vals
    static_assert(is_in_tuple_v<Model, ModelsTuple>, "FieldPtr from a model not specified in BasePredicate<...>");
    return BasePredicate(fieldPtr, Operator::IN, vals, tableAlias);
  }

  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> in(FieldT Model::*fieldPtr, SelectQuery<Models...> &subQuery,
                                                     const std::string &tableAlias = "") { // field-subquery
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "equals(): FieldPtr from a model not specified in BasePredicate<...>");
    if (subQuery.getColumnCount() != 1)
      throw OrmException("IN subquery must have one specified field (use yourQuery.field())");
    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::IN, subQuery, tableAlias);
  }

  //===============NOT IN===============
  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> notIn(FieldT Model::*fieldPtr,
                                                        const std::vector<remove_optional_t<FieldT>> &vals,
                                                        const std::string &tableAlias = "") { // field - values
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "notIn(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::NOT_IN, vals, tableAlias);
  }

  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> notIn(FieldT Model::*fieldPtr, SelectQuery<Models...> &subQuery,
                                                        const std::string &tableAlias = "") { // field - subquery
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "notIn(): FieldPtr from a model not specified in BasePredicate<...>");

    if (subQuery.getColumnCount() != 1)
      throw OrmException("NOT IN subquery must have one specified field (use yourQuery.field())");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::NOT_IN, subQuery, tableAlias);
  }

  //===============NOT EQUALS===============
  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> notEquals(FieldT Model::*fieldPtr,
                                                            const remove_optional_t<FieldT> &val,
                                                            const std::string &tableAlias = "") { // field - val
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "notEquals(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::NOT_EQUALS, {val}, tableAlias);
  }

  template <typename FieldTA, typename FieldTB, typename ModelA, typename ModelB>
  static BasePredicate<ColumnResolver, Models...>
  notEquals(FieldTA ModelA::*fieldPtrA, FieldTB ModelB::*fieldPtrB,
            const std::pair<std::string, std::string> tableAliases = {}) { // field - field
    static_assert(is_in_tuple_v<ModelA, ModelsTuple>,
                  "notEquals(): FieldPtr from a model not specified in BasePredicate<...>");
    static_assert(is_in_tuple_v<ModelB, ModelsTuple>, "FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtrA, Operator::NOT_EQUALS, fieldPtrB, tableAliases);
  }

  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> notEquals(FieldT Model::*fieldPtr, SelectQuery<Models...> &subQuery,
                                                            const std::string &tableAlias = "") { // field - subquery
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "notEquals(): FieldPtr from a model not specified in BasePredicate<...>");

    validateScalarSubQuery(subQuery, "NOT EQUALS");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::NOT_EQUALS, subQuery, tableAlias);
  }

  //===============GREATER===============
  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> greater(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val,
                                                          const std::string &tableAlias = "") { // field - val
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "greater(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::GREATER, {val}, tableAlias);
  }

  template <typename FieldTA, typename FieldTB, typename ModelA, typename ModelB>
  static BasePredicate<ColumnResolver, Models...>
  greater(FieldTA ModelA::*fieldPtrA, FieldTB ModelB::*fieldPtrB,
          const std::pair<std::string, std::string> tableAliases = {}) { // field - field
    static_assert(is_in_tuple_v<ModelA, ModelsTuple>,
                  "greater(): FieldPtr from a model not specified in BasePredicate<...>");
    static_assert(is_in_tuple_v<ModelB, ModelsTuple>,
                  "greater(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtrA, Operator::GREATER, fieldPtrB, tableAliases);
  }

  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> greater(FieldT Model::*fieldPtr, SelectQuery<Models...> &subQuery,
                                                          const std::string &tableAlias = "") { // field - subquery
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "greater(): FieldPtr from a model not specified in BasePredicate<...>");

    validateScalarSubQuery(subQuery, "GREATER");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::GREATER, subQuery, tableAlias);
  }

  //===============LESSER===============
  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> lesser(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val,
                                                         const std::string &tableAlias = "") { // field - val
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "lesser(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::LESSER, {val}, tableAlias);
  }

  template <typename FieldTA, typename FieldTB, typename ModelA, typename ModelB>
  static BasePredicate<ColumnResolver, Models...>
  lesser(FieldTA ModelA::*fieldPtrA, FieldTB ModelB::*fieldPtrB,
         const std::pair<std::string, std::string> tableAliases = {}) { // field - field
    static_assert(is_in_tuple_v<ModelA, ModelsTuple>,
                  "lesser(): FieldPtr from a model not specified in BasePredicate<...>");
    static_assert(is_in_tuple_v<ModelB, ModelsTuple>,
                  "lesser(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtrA, Operator::LESSER, fieldPtrB, tableAliases);
  }

  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> lesser(FieldT Model::*fieldPtr, SelectQuery<Models...> &subQuery,
                                                         const std::string &tableAlias = "") { // field - subquery
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "lesser(): FieldPtr from a model not specified in BasePredicate<...>");

    validateScalarSubQuery(subQuery, "LESSER");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::LESSER, subQuery, tableAlias);
  }

  //===============GREATER OR EQUAL===============
  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> greaterOrEqual(FieldT Model::*fieldPtr,
                                                                 const remove_optional_t<FieldT> &val,
                                                                 const std::string &tableAlias = "") { // field - val
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "greaterOrEqual(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::GREATER_OR_EQUAL, {val}, tableAlias);
  }

  template <typename FieldTA, typename FieldTB, typename ModelA, typename ModelB>
  static BasePredicate<ColumnResolver, Models...>
  greaterOrEqual(FieldTA ModelA::*fieldPtrA, FieldTB ModelB::*fieldPtrB,
                 const std::pair<std::string, std::string> tableAliases = {}) { // field - field
    static_assert(is_in_tuple_v<ModelA, ModelsTuple>,
                  "greaterOrEqual(): FieldPtr from a model not specified in BasePredicate<...>");
    static_assert(is_in_tuple_v<ModelB, ModelsTuple>,
                  "greaterOrEqual(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtrA, Operator::GREATER_OR_EQUAL, fieldPtrB, tableAliases);
  }

  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...>
  greaterOrEqual(FieldT Model::*fieldPtr, SelectQuery<Models...> &subQuery,
                 const std::string &tableAlias = "") { // field - subquery
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "greaterOrEqual(): FieldPtr from a model not specified in BasePredicate<...>");

    validateScalarSubQuery(subQuery, "GREATER OR EQUAL");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::GREATER_OR_EQUAL, subQuery, tableAlias);
  }

  //===============LESSER OR EQUAL===============
  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...> lesserOrEqual(FieldT Model::*fieldPtr,
                                                                const remove_optional_t<FieldT> &val,
                                                                const std::string &tableAlias = "") { // field - val
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "lesserOrEqual(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::LESSER_OR_EQUAL, {val}, tableAlias);
  }

  template <typename FieldTA, typename FieldTB, typename ModelA, typename ModelB>
  static BasePredicate<ColumnResolver, Models...>
  lesserOrEqual(FieldTA ModelA::*fieldPtrA, FieldTB ModelB::*fieldPtrB,
                const std::pair<std::string, std::string> tableAliases = {}) { // field - field
    static_assert(is_in_tuple_v<ModelA, ModelsTuple>,
                  "lesserOrEqual(): FieldPtr from a model not specified in BasePredicate<...>");
    static_assert(is_in_tuple_v<ModelB, ModelsTuple>,
                  "lesserOrEqual(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtrA, Operator::LESSER_OR_EQUAL, fieldPtrB, tableAliases);
  }

  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...>
  lesserOrEqual(FieldT Model::*fieldPtr, SelectQuery<Models...> &subQuery,
                const std::string &tableAlias = "") { // field - subquery
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "lesserOrEqual(): FieldPtr from a model not specified in BasePredicate<...>");

    validateScalarSubQuery(subQuery, "LESSER OR EQUAL");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::LESSER_OR_EQUAL, subQuery, tableAlias);
  }

  //===============LIKE===============
  template <typename FieldT, typename Model>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static BasePredicate<ColumnResolver, Models...> like(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val,
                                                       const std::string &tableAlias = "") { // field - val
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "like(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::LIKE, {val}, tableAlias);
  }

  //===============ILIKE===============
  template <typename FieldT, typename Model>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static BasePredicate<ColumnResolver, Models...> ilike(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val,
                                                        const std::string &tableAlias = "") {
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "ilike(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::LIKE, {val}, "LOWER", "LOWER", tableAlias);
  }

  //===============CONTAINS===============
  template <typename FieldT, typename Model>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static BasePredicate<ColumnResolver, Models...>
  contains(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val, const std::string &tableAlias = "") {
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "contains(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::LIKE, {'%' + val + '%'}, tableAlias);
  }

  //===============ICONTAINS===============
  template <typename FieldT, typename Model>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static BasePredicate<ColumnResolver, Models...>
  iContains(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val, const std::string &tableAlias = "") {
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "iContains(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::LIKE, {'%' + val + '%'}, "LOWER", "LOWER",
                                                    tableAlias);
  }

  //===============STARTS WITH===============
  template <typename FieldT, typename Model>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static BasePredicate<ColumnResolver, Models...>
  startsWith(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val, const std::string &tableAlias = "") {
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "startsWith(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::LIKE, {val + '%'}, tableAlias);
  }

  //===============ENDS WITH===============
  template <typename FieldT, typename Model>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static BasePredicate<ColumnResolver, Models...>
  endsWith(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val, const std::string &tableAlias = "") {
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "endsWith(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::LIKE, {'%' + val}, tableAlias);
  }

  //===============BETWEEN===============
  template <typename FieldT, typename Model>
  static BasePredicate<ColumnResolver, Models...>
  between(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val1, const remove_optional_t<FieldT> &val2,
          const std::string &tableAlias = "") { // field - val1 - val2
    static_assert(is_in_tuple_v<Model, ModelsTuple>,
                  "between(): FieldPtr from a model not specified in BasePredicate<...>");

    return BasePredicate<ColumnResolver, Models...>(fieldPtr, Operator::BETWEEN, {val1, val2}, tableAlias);
  }

  //===============HELPERS END===============

  std::string resolvePredicates(std::vector<db::DbValue> &out_params) const {
    switch (predicateType) {

    case PredicateType::LEAF: {
      std::string predicateString = " ";

      std::string lhs = columnA;
      if (not lhsFunction.empty())
        lhs = lhsFunction + "(" + lhs + ")";

      if (op == Operator::IS_NULL || op == Operator::IS_NOT_NULL) {
        predicateString += lhs + " " + std::string(to_string(op));
        return predicateString;
      }

      if (not isGroupOperator(op)) {
        out_params.push_back(values[0]);

        std::string rhs = "?";
        if (not rhsFunction.empty())
          rhs = rhsFunction + "(" + rhs + ")";

        predicateString += lhs + " " + std::string(to_string(op)) + " " + rhs + " ";

        if (op == Operator::LIKE)
          predicateString += " ESCAPE '\\' ";
      }

      else { // group operators
        if (op == Operator::BETWEEN) {
          if (values.size() != 2)
            throw rukh::OrmException("BETWEEN predicate should have 2 values");

          out_params.push_back(values[0]);
          out_params.push_back(values[1]);

          std::string rhs1 = "?";
          std::string rhs2 = "?";

          if (not rhsFunction.empty()) {
            rhs1 = rhsFunction + "(" + rhs1 + ")";
            rhs2 = rhsFunction + "(" + rhs2 + ")";
          }

          return lhs + " BETWEEN " + rhs1 + " AND " + rhs2;
        }

        predicateString += lhs + " " + std::string(to_string(op)) + " ( ";

        if (not values.empty()) {
          out_params.push_back(values[0]);
          predicateString += " ? ";
        }

        for (size_t i = 1; i < values.size(); i++) {
          out_params.push_back(values[i]);
          predicateString += ", ? ";
        }

        predicateString += " ) ";
      }

      return predicateString;
    }

    case PredicateType::STRING: {
      out_params.insert(out_params.end(), values.begin(), values.end());
      return ' ' + columnA + ' ';
    }

    case PredicateType::TRUE: {
      return " TRUE ";
    }

    case PredicateType::FALSE: {
      return " FALSE ";
    }

    case PredicateType::FIELD_COMPARISON: {
      return " " + columnA + " " + std::string(to_string(op)) + " " + columnB + " ";
    }

    case PredicateType::SUBQUERY: {
      out_params.insert(out_params.end(), values.begin(), values.end());
      return lhsFunction + "(" + columnA + ") " + std::string(to_string(op)) + " ( " + columnB + " ) ";
    }

    case PredicateType::EXISTS: {
      out_params.insert(out_params.end(), values.begin(), values.end());
      return " EXISTS (" + columnA + ") ";
    }

    case PredicateType::NOT_EXISTS: {
      out_params.insert(out_params.end(), values.begin(), values.end());
      return " NOT EXISTS (" + columnA + ") ";
    }

    case PredicateType::RAW: {
      out_params.insert(out_params.end(), values.begin(), values.end());
      return " " + columnA + " " + std::string(to_string(op)) + " " + columnB + " ";
    }

    default: {
      std::string s = "(";

      s += children[0].resolvePredicates(out_params);

      if (predicateType == PredicateType::AND)
        s += " AND ";
      else if (predicateType == PredicateType::OR)
        s += " OR ";

      s += children[1].resolvePredicates(out_params);
      s += ")";

      return s;
    }
    }
  }

  std::string toString() const {
    std::string predicateString = "( ";

    switch (predicateType) {

    case PredicateType::LEAF: {
      std::string lhs = columnA;

      if (not lhsFunction.empty())
        lhs = lhsFunction + "(" + lhs + ")";

      if (op == Operator::IS_NULL || op == Operator::IS_NOT_NULL) { // IS_NULL or IS_NOT_NULL
        predicateString += lhs + " " + std::string(to_string(op));
        return predicateString + " )";
      }

      if (not isGroupOperator(op)) { // non-group operators
        std::string rhs = "?";

        if (not rhsFunction.empty())
          rhs = rhsFunction + "(" + rhs + ")";

        predicateString += lhs + " " + std::string(to_string(op)) + " " + rhs + " ";

        if (op == Operator::LIKE)
          predicateString += " ESCAPE '\\' ";

        return predicateString + ")";
      }

      // group operators
      if (op == Operator::BETWEEN) {
        if (values.size() != 2)
          throw rukh::OrmException("BETWEEN predicate should have 2 values");

        std::string rhs1 = "?";
        std::string rhs2 = "?";

        if (not rhsFunction.empty()) {
          rhs1 = rhsFunction + "(" + rhs1 + ")";
          rhs2 = rhsFunction + "(" + rhs2 + ")";
        }

        predicateString += lhs + " BETWEEN " + rhs1 + " AND " + rhs2;

        return predicateString + " )";
      }

      predicateString += lhs + " " + std::string(to_string(op)) + " ( ";

      if (not values.empty())
        predicateString += " ? ";

      for (size_t i = 1; i < values.size(); ++i)
        predicateString += ", ? ";

      predicateString += " )";

      return predicateString + " )";
    }

    case PredicateType::STRING: {
      predicateString += columnA + " )";
      return predicateString;
    }

    case PredicateType::TRUE: {
      return " TRUE ";
    }

    case PredicateType::FALSE: {
      return " FALSE ";
    }

    case PredicateType::FIELD_COMPARISON: {
      return " " + columnA + " " + std::string(to_string(op)) + " " + columnB + " ";
    }

    case PredicateType::SUBQUERY: {
      return " " + columnA + " " + std::string(to_string(op)) + " ( " + columnB + " ) ";
    }

    default: {
      std::string s = "(";

      s += children[0].toString();

      if (predicateType == PredicateType::AND)
        s += " AND ";
      else if (predicateType == PredicateType::OR)
        s += " OR ";

      s += children[1].toString();
      s += ")";

      return s;
    }
    }
  }

private:
  template <FieldPointer FieldPtr>
  static std::string getColumnWithAlias(FieldPtr fieldPtr, const std::string &tableAlias) {
    return ColumnResolver::template resolve<Models...>(fieldPtr, tableAlias);
  }

  static void validateScalarSubQuery(SelectQuery<Models...> &subQuery, const std::string &operatorName) {
    if (subQuery.getLimit().value_or(0) != 1)
      throw OrmException(operatorName + " subquery must have a limit of 1");

    if (subQuery.getColumnCount() != 1)
      throw OrmException(operatorName + " subquery must have one specified field (use yourQuery.field())");
  }
};

struct DefaultColumnResolver {
  template <typename... Models, FieldPointer FieldPtr>
  static std::string resolve(FieldPtr fieldPtr, const std::string &tableAlias) {

    using FieldPtrModel = get_class_t<FieldPtr>;

    if (tableAlias.empty())
      return getAlias(get_index_of_v<FieldPtrModel, std::tuple<Models...>>) + "." +
             FieldPtrModel::columnNameOf(fieldPtr);

    return tableAlias + "." + FieldPtrModel::columnNameOf(fieldPtr);
  }
};

template <typename... Models> using Predicate = BasePredicate<DefaultColumnResolver, Models...>;

} // namespace rukh::orm
