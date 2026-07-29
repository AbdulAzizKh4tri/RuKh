#pragma once

#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/core/utils.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Column.hpp>
#include <rukh/orm/TypeHelpers.hpp>

namespace rukh::orm {

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

static constexpr std::string_view opStrings[] = {
    "=", "!=", ">", "<", ">=", "<=", "LIKE", "IN", "NOT IN", "IS NULL", "IS NOT NULL", "BETWEEN"};
constexpr std::string_view to_string(Operator op) { return opStrings[static_cast<std::size_t>(op)]; }

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

template <typename Model> struct Predicate {
  enum class PredicateType { LEAF, AND, OR, TRUE, FALSE, STRING } predicateType;

  std::string column, customString;
  Operator op;                     // only used if
  std::vector<db::DbValue> values; // Kind::Leaf
                                   //
  std::vector<Predicate> children; // used if And/Or

  //===============CONSTRUCTORS===============

  Predicate(const std::string &str, const std::vector<db::DbValue> &values)
      : predicateType(PredicateType::STRING), customString(str), values(values) {}

  Predicate(const std::string &str, const db::DbValue &values)
      : predicateType(PredicateType::STRING), customString(str), values({values}) {}

  Predicate(bool val) {
    if (val)
      predicateType = PredicateType::TRUE;
    else
      predicateType = PredicateType::FALSE;
  }

  Predicate(const std::string &col, Operator opr) : predicateType(PredicateType::LEAF) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");

    if (not(opr == Operator::IS_NULL || opr == Operator::IS_NOT_NULL))
      throw rukh::OrmException("Predicate Construction: must provide operands");

    column = col;
    op = opr;
  }

  Predicate(const std::string &col, Operator opr, const db::DbValue &val) : predicateType(PredicateType::LEAF) {

    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");

    if (isGroupOperator(opr))
      throw rukh::OrmException("Predicate Construction with group operator: provide vector of values");

    column = col;
    op = opr;
    values.push_back(val);
  }

  Predicate(const std::string &col, Operator opr, const std::vector<db::DbValue> &vals)
      : predicateType(PredicateType::LEAF) {

    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");

    if (not isGroupOperator(opr))
      throw rukh::OrmException(
          "Predicate Construction with vector: Only group operators should be provided with vectors.");

    op = opr;
    column = col;
    values = vals;
  }

  template <typename FieldT>
  Predicate(FieldT Model::*fieldPtr, Operator op, const remove_optional_t<FieldT> &val)
      : Predicate(Model::columnNameOf(fieldPtr), op, val) {}

  template <typename FieldT>
  Predicate(FieldT Model::*fieldPtr, Operator op, const std::vector<remove_optional_t<FieldT>> &vals)
      : Predicate(Model::columnNameOf(fieldPtr), op, vals) {}

  Predicate(PredicateType k) : predicateType(k) {}

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

  static Predicate<Model> truePredicate() { return Predicate(true); }
  static Predicate<Model> falsePredicate() { return Predicate(false); }

  static Predicate<Model> in(const std::string &col, const std::vector<db::DbValue> &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::IN, val);
  }
  template <typename FieldT>
  static Predicate<Model> in(FieldT Model::*fieldPtr, const std::vector<remove_optional_t<FieldT>> &val) {
    return in(Model::columnNameOf(fieldPtr), std::vector<db::DbValue>(val.begin(), val.end()));
  }

  static Predicate<Model> notIn(const std::string &col, const std::vector<db::DbValue> &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate<Model>(col, Operator::NOT_IN, val);
  }
  template <typename FieldT>
  static Predicate<Model> notIn(FieldT Model::*fieldPtr, const std::vector<remove_optional_t<FieldT>> &val) {
    return notIn(Model::columnNameOf(fieldPtr), std::vector<db::DbValue>(val.begin(), val.end()));
  }

  static Predicate<Model> equals(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::EQUALS, val);
  }
  template <typename FieldT>
  static Predicate<Model> equals(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return equals(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> notEquals(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::NOT_EQUALS, val);
  }
  template <typename FieldT>
  static Predicate<Model> notEquals(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return notEquals(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> greater(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::GREATER, val);
  }
  template <typename FieldT>
  static Predicate<Model> greater(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return greater(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> lesser(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::LESSER, val);
  }
  template <typename FieldT>
  static Predicate<Model> lesser(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return lesser(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> greaterOrEqual(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::GREATER_OR_EQUAL, val);
  }
  template <typename FieldT>
  static Predicate<Model> greaterOrEqual(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return greaterOrEqual(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> lesserOrEqual(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::LESSER_OR_EQUAL, val);
  }
  template <typename FieldT>
  static Predicate<Model> lesserOrEqual(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return lesserOrEqual(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> like(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::LIKE, val);
  }
  template <typename FieldT>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static Predicate<Model> like(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return like(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> ilike(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");

    return Predicate(" LOWER(" + col + ") LIKE LOWER(?) ESCAPE '\\'", db::dbValueToString(val));
  }
  template <typename FieldT>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static Predicate<Model> ilike(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return ilike(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> contains(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::LIKE, '%' + db::dbValueToString(val) + '%');
  }
  template <typename FieldT>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static Predicate<Model> contains(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return contains(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> iContains(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate("LOWER(" + col + ") LIKE LOWER(?) ESCAPE '\\'", '%' + db::dbValueToString(val) + '%');
  }
  template <typename FieldT>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static Predicate<Model> iContains(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return iContains(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> startsWith(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::LIKE, db::dbValueToString(val) + '%');
  }
  template <typename FieldT>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static Predicate<Model> startsWith(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return startsWith(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> endsWith(const std::string &col, const db::DbValue &val) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate(col, Operator::LIKE, '%' + db::dbValueToString(val));
  }
  template <typename FieldT>
    requires std::is_same_v<remove_optional_t<FieldT>, std::string>
  static Predicate<Model> endsWith(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val) {
    return endsWith(Model::columnNameOf(fieldPtr), val);
  }

  static Predicate<Model> between(const std::string &col, const db::DbValue &val1, const db::DbValue &val2) {
    if (not Model::isValidColumnName(col))
      throw rukh::OrmException("Predicate Construction: Invalid column name.");
    return Predicate<Model>(col, Operator::BETWEEN, {val1, val2});
  }
  template <typename FieldT>
  static Predicate<Model> between(FieldT Model::*fieldPtr, const remove_optional_t<FieldT> &val1,
                                  const remove_optional_t<FieldT> &val2) {
    return between(Model::columnNameOf(fieldPtr), val1, val2);
  }

  static std::string resolvePredicates(const Predicate &p, std::vector<db::DbValue> &out_params) {
    if (p.predicateType == Predicate::PredicateType::LEAF) {
      std::string predicateString = " ";

      if (p.values.empty()) {
        predicateString += p.column + " " + std::string(to_string(p.op));
      } else if (p.values.size() == 1) {
        out_params.push_back(p.values[0]);
        predicateString += p.column + " " + std::string(to_string(p.op)) + " ? ";

        if (p.op == Operator::LIKE)
          predicateString += " ESCAPE '\\' ";

      } else {
        if (p.op == Operator::BETWEEN) {
          if (p.values.size() != 2)
            throw rukh::OrmException("BETWEEN predicate should have 2 values");
          out_params.push_back(p.values[0]);
          out_params.push_back(p.values[1]);
          return p.column + " BETWEEN ? AND ?";
        }
        predicateString += p.column + " " + std::string(to_string(p.op)) + " ( ";
        out_params.push_back(p.values[0]);
        predicateString += " ? ";

        for (int i = 1; i < p.values.size(); i++) {
          out_params.push_back(p.values[i]);
          predicateString += ", ? ";
        }
        predicateString += " ) ";
      }
      return predicateString;
    } else if (p.predicateType == Predicate::PredicateType::STRING) {
      out_params.insert(out_params.end(), p.values.begin(), p.values.end());
      return ' ' + p.customString + ' ';
    } else if (p.predicateType == Predicate::PredicateType::TRUE) {
      return " TRUE ";
    } else if (p.predicateType == Predicate::PredicateType::FALSE) {
      return " FALSE ";
    }

    std::string s = "(";
    if (p.children.size() != 2)
      throw rukh::OrmException("Non-leaf predicate with wrong number of children");
    s += resolvePredicates(p.children[0], out_params);
    if (p.predicateType == Predicate::PredicateType::AND)
      s += " AND ";
    else if (p.predicateType == Predicate::PredicateType::OR)
      s += " OR ";
    s += resolvePredicates(p.children[1], out_params);
    s += ")";
    return s;
  }

  std::string toString() {
    std::string predicateString = "( ";

    if (predicateType == Predicate::PredicateType::LEAF) {
      predicateString += column + " " + std::string(to_string(op)) + "{ ";
      for (auto &val : values) {
        predicateString += " " + db::dbValueToString(val);
      }
      predicateString += " })";
      return predicateString;

    } else if (predicateType == Predicate::PredicateType::STRING) {
      predicateString += customString + " )";
      return predicateString;
    } else if (predicateType == Predicate::PredicateType::TRUE) {
      return " TRUE ";
    } else if (predicateType == Predicate::PredicateType::FALSE) {
      return " FALSE ";
    }

    std::string s = "(";
    if (children.size() != 2)
      throw rukh::OrmException("Non-leaf predicate with wrong number of children");

    s += children[0].toString();

    if (predicateType == Predicate::PredicateType::AND)
      s += " AND ";
    else if (predicateType == Predicate::PredicateType::OR)
      s += " OR ";

    s += children[1].toString();
    s += ")";
    return s;
  }
};

} // namespace rukh::orm
