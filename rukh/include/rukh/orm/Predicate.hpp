#pragma once

#include "rukh/db/DbValue.hpp"
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/TypeHelpers.hpp>
#include <rukh/core/utils.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Column.hpp>

namespace rukh::orm {

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

template <typename... Models> struct Predicate {
  using ModelsTuple = std::tuple<Models...>;

  static constexpr std::string_view opStrings[] = {
      "=", "!=", ">", "<", ">=", "<=", "LIKE", "IN", "NOT IN", "IS NULL", "IS NOT NULL", "BETWEEN"};
  static constexpr std::string_view to_string(Operator op) { return opStrings[static_cast<std::size_t>(op)]; }

  constexpr bool isGroupOperator(Operator op) {
    switch (op) {
    case Operator::IN:
    case Operator::NOT_IN:
    case Operator::BETWEEN:
      return true;
    default:
      return false;
    }
  }

  enum class PredicateType { LEAF, AND, OR, TRUE, FALSE, STRING, FIELD_COMPARISON } predicateType;

  std::string columnA;
  std::string columnB;

  Operator op;                     // only used if
  std::vector<db::DbValue> values; // Kind::Leaf

  std::vector<Predicate<Models...>> children; // used for And/Or

  //===============CONSTRUCTORS===============

  Predicate(const std::string &str, const std::vector<db::DbValue> &values)
      : predicateType(PredicateType::STRING), columnA(str), values(values) {}

  Predicate(const std::string &str, const db::DbValue &value)
      : predicateType(PredicateType::STRING), columnA(str), values({value}) {}

  Predicate(bool val) {
    if (val)
      predicateType = PredicateType::TRUE;
    else
      predicateType = PredicateType::FALSE;
  }

  Predicate(const std::string &col, Operator opr) : predicateType(PredicateType::LEAF) {
    if (not(opr == Operator::IS_NULL || opr == Operator::IS_NOT_NULL))
      throw rukh::OrmException("Predicate Construction: must provide operands");
    columnA = col;
    op = opr;
  }

  Predicate(const std::string &col, Operator opr, const db::DbValue &val) : predicateType(PredicateType::LEAF) {
    if (isGroupOperator(opr))
      throw rukh::OrmException("Predicate Construction with group operator: provide vector of values");
    columnA = col;
    op = opr;
    values.push_back(val);
  }

  Predicate(const std::string &col, Operator opr, const std::vector<db::DbValue> &vals)
      : predicateType(PredicateType::LEAF) {
    if (not isGroupOperator(opr))
      throw rukh::OrmException(
          "Predicate Construction with vector: Only group operators should be provided with vectors.");
    op = opr;
    columnA = col;
    values = vals;
  }

  template <typename FieldPtr>
  Predicate(FieldPtr fieldPtr, Operator opr, const get_raw_field_t<FieldPtr> &val, const std::string &tableAlias = "")
      : Predicate(getColumnWithTableAlias(fieldPtr, tableAlias), opr, val) {}

  template <typename FieldPtr>
  Predicate(FieldPtr fieldPtr, Operator opr, const std::vector<get_raw_field_t<FieldPtr>> &vals,
            const std::string &tableAlias = "")
      : Predicate(getColumnWithTableAlias(fieldPtr, tableAlias), opr, vals) {}

  /*
   * Table aliases are optional. Some "joins" may require them if there is ambiguity.
   */
  template <typename FieldTA, typename FieldTB, typename ModelA, typename ModelB>
  Predicate(FieldTA ModelA::*fieldPtrA, Operator opr, FieldTB ModelB::*fieldPtrB,
            std::pair<std::string, std::string> tableAliases = {})
      : op(opr), predicateType(PredicateType::FIELD_COMPARISON) {

    columnA = getColumnWithTableAlias(fieldPtrA, tableAliases.first);
    columnB = getColumnWithTableAlias(fieldPtrB, tableAliases.second);
  }

  Predicate(PredicateType k) : predicateType(k) {}

  //===============OPERATORS===============

  Predicate operator||(const Predicate &rhs) {
    Predicate p(PredicateType::OR);
    p.children.push_back(*this);
    p.children.push_back(rhs);
    return p;
  }

  Predicate operator&&(const Predicate &rhs) {
    Predicate p(PredicateType::AND);
    p.children.push_back(*this);
    p.children.push_back(rhs);
    return p;
  }

  //===============HELPERS===============

  //===============EQUALS===============
  template <typename FieldT, typename Model>
  static Predicate<Models...> equals(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    static_assert(is_in_tuple_v<Model, ModelsTuple>, "equals(): FieldPtr from a model not specified in Predicate<...>");

    return Predicate<Models...>(fieldPtr, Operator::EQUALS, val);
  }

  template <typename FieldTA, typename FieldTB, typename ModelA, typename ModelB>
  static Predicate<Models...> equals(FieldTA ModelA::*fieldPtrA, FieldTB ModelB::*fieldPtrB) {
    static_assert(is_in_tuple_v<ModelA, ModelsTuple>,
                  "equals(): FieldPtr from a model not specified in Predicate<...>");
    static_assert(is_in_tuple_v<ModelB, ModelsTuple>,
                  "equals(): FieldPtr from a model not specified in Predicate<...>");
    return Predicate<Models...>(fieldPtrA, Operator::EQUALS, fieldPtrB);
  }

  //===============IN===============
  template <typename FieldPtr>
  static Predicate<Models...> in(FieldPtr fieldPtr, const std::vector<remove_optional_t<FieldPtr>> &val) {
    using Model = get_class_t<FieldPtr>;
    static_assert(is_in_tuple_v<Model, ModelsTuple>, "FieldPtr from a model not specified in Predicate<...>");
    return in(Model::columnNameOf(fieldPtr), std::vector<db::DbValue>(val.begin(), val.end()));
  }

  std::string resolvePredicates(std::vector<db::DbValue> &out_params) {
    switch (predicateType) {

    case PredicateType::LEAF: {
      std::string predicateString = " ";
      if (values.empty()) {
        predicateString += columnA + " " + std::string(to_string(op));
      } else if (values.size() == 1) {
        out_params.push_back(values[0]);
        predicateString += columnA + " " + std::string(to_string(op)) + " ? ";

        if (op == Operator::LIKE)
          predicateString += " ESCAPE '\\' ";

      } else {
        if (op == Operator::BETWEEN) {
          if (values.size() != 2)
            throw rukh::OrmException("BETWEEN predicate should have 2 values");
          out_params.push_back(values[0]);
          out_params.push_back(values[1]);
          return columnA + " BETWEEN ? AND ?";
        }
        predicateString += columnA + " " + std::string(to_string(op)) + " ( ";
        out_params.push_back(values[0]);
        predicateString += " ? ";

        for (int i = 1; i < values.size(); i++) {
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
    default: {

      std::string s = "(";
      if (children.size() != 2)
        throw rukh::OrmException("Non-leaf predicate with wrong number of children");

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

      predicateString += columnA + " " + std::string(to_string(op)) + "{ ";
      for (auto &val : values) {
        predicateString += " " + db::dbValueToString(val);
      }
      predicateString += " })";
      return predicateString;
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
      return " " + this->columnA + " " + std::string(to_string(this->op)) + " " + this->columnB + " ";
    }
    default: {
      std::string s = "(";
      if (children.size() != 2)
        throw rukh::OrmException("Non-leaf predicate with wrong number of children");

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
  template <typename FieldPtr> std::string getColumnWithTableAlias(FieldPtr fieldPtr, const std::string &tableAlias) {
    using FieldPtrModel = get_class_t<FieldPtr>;
    if (tableAlias.empty())
      return getAlias(get_index_of_v<FieldPtrModel, ModelsTuple>) + "." + FieldPtrModel::columnNameOf(fieldPtr);
    return tableAlias + "." + FieldPtrModel::columnNameOf(fieldPtr);
  }
};

} // namespace rukh::orm

//
//
// //===============NOT IN===============
// static Predicate<Model> notIn(const std::string &col, const std::vector<db::DbValue> &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate<Model>(col, Operator::NOT_IN, val);
// }
// template <typename FieldT>
// static Predicate<Model> notIn(FieldT Model::*fieldPtr, const std::vector<remove_optional_t<FieldT>> &val) {
//   return notIn(Model::columnNameOf(fieldPtr), std::vector<db::DbValue>(val.begin(), val.end()));
// }
//
//
// //===============NOT EQUALS===============
// static Predicate<Model> notEquals(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate(col, Operator::NOT_EQUALS, val);
// }
// template <typename FieldT>
// static Predicate<Model> notEquals(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return notEquals(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============GREATER===============
// static Predicate<Model> greater(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate(col, Operator::GREATER, val);
// }
// template <typename FieldT>
// static Predicate<Model> greater(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return greater(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============LESSER===============
// static Predicate<Model> lesser(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate(col, Operator::LESSER, val);
// }
// template <typename FieldT>
// static Predicate<Model> lesser(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return lesser(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============GREATER OR EQUAL===============
// static Predicate<Model> greaterOrEqual(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate(col, Operator::GREATER_OR_EQUAL, val);
// }
// template <typename FieldT>
// static Predicate<Model> greaterOrEqual(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return greaterOrEqual(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============LESSER OR EQUAL===============
// static Predicate<Model> lesserOrEqual(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate(col, Operator::LESSER_OR_EQUAL, val);
// }
// template <typename FieldT>
// static Predicate<Model> lesserOrEqual(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return lesserOrEqual(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============LIKE===============
// static Predicate<Model> like(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate(col, Operator::LIKE, val);
// }
// template <typename FieldT>
//   requires std::is_same_v<remove_optional_t<FieldT>, std::string>
// static Predicate<Model> like(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return like(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============ILIKE===============
// static Predicate<Model> ilike(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//
//   return Predicate(" LOWER(" + col + ") LIKE LOWER(?) ESCAPE '\\'", db::dbValueToString(val));
// }
// template <typename FieldT>
//   requires std::is_same_v<remove_optional_t<FieldT>, std::string>
// static Predicate<Model> ilike(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return ilike(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============CONTAINS===============
// static Predicate<Model> contains(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate(col, Operator::LIKE, '%' + db::dbValueToString(val) + '%');
// }
// template <typename FieldT>
//   requires std::is_same_v<remove_optional_t<FieldT>, std::string>
// static Predicate<Model> contains(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return contains(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============ICONTAINS===============
// static Predicate<Model> iContains(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate("LOWER(" + col + ") LIKE LOWER(?) ESCAPE '\\'", '%' + db::dbValueToString(val) + '%');
// }
// template <typename FieldT>
//   requires std::is_same_v<remove_optional_t<FieldT>, std::string>
// static Predicate<Model> iContains(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return iContains(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============STARTS WITH===============
// static Predicate<Model> startsWith(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate(col, Operator::LIKE, db::dbValueToString(val) + '%');
// }
// template <typename FieldT>
//   requires std::is_same_v<remove_optional_t<FieldT>, std::string>
// static Predicate<Model> startsWith(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return startsWith(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============ENDS WITH===============
// static Predicate<Model> endsWith(const std::string &col, const db::DbValue &val) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate(col, Operator::LIKE, '%' + db::dbValueToString(val));
// }
// template <typename FieldT>
//   requires std::is_same_v<remove_optional_t<FieldT>, std::string>
// static Predicate<Model> endsWith(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
//   return endsWith(Model::columnNameOf(fieldPtr), val);
// }
//
// //===============BETWEEN===============
// static Predicate<Model> between(const std::string &col, const db::DbValue &val1, const db::DbValue &val2) {
//   if (not Model::isValidColumnName(col))
//     throw rukh::OrmException("Predicate Construction: Invalid column name.");
//   return Predicate<Model>(col, Operator::BETWEEN, {val1, val2});
// }
// template <typename FieldT>
// static Predicate<Model> between(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val1,
//                                 const remove_optional_t<FieldT> &val2) {
//   return between(Model::columnNameOf(fieldPtr), val1, val2);
// }
// //===============HELPERS END===================================================================
