#pragma once

#include <rukh/Exceptions.hpp>
#include <rukh/db/IDatabase.hpp>

namespace rukh::orm {

struct Predicate {
  enum class PredicateType { LEAF, AND, OR } predicateType;
  static constexpr std::string validOps[] = {"=", "!=", ">", "<", ">=", "<=", "LIKE", "IN"};

  std::string column, op;          // only used if
  std::vector<db::DbValue> values; // Kind::Leaf

  std::vector<Predicate> children; // used if And/Or

  Predicate(const std::string &col, const std::string &op, const db::DbValue &val)
      : predicateType(PredicateType::LEAF), column(col) {

    if (op == "IN")
      throw rukh::OrmException("Predicate Construction with 'IN' operator: provide vector of values");

    bool valid = false;
    for (auto &v : validOps) {
      if (v == op) {
        valid = true;
        break;
      }
    }

    if (not valid)
      throw rukh::OrmException("Predicate Construction: Invalid operator.");

    this->op = op;
    values.push_back(val);
  }

  Predicate(const std::string &col, const std::string &op, const std::vector<db::DbValue> &val)
      : predicateType(PredicateType::LEAF), column(col), op(op) {
    if (op != "IN")
      throw rukh::OrmException("Predicate Construction with vector: Only IN operator is supported.");

    values = val;
  }

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

  static std::string resolvePredicates(Predicate p, std::vector<db::DbValue> &out_params) {
    if (p.predicateType == Predicate::PredicateType::LEAF) {
      std::string predicateString = " ";
      if (p.values.size() == 1) {
        out_params.push_back(p.values[0]);
        predicateString += p.column + " " + p.op + " ? ";
      } else if (p.values.size() > 1) {
        predicateString += p.column + " " + p.op + " ( ";
        out_params.push_back(p.values[0]);
        predicateString += " ? ";

        for (int i = 1; i < p.values.size(); i++) {
          out_params.push_back(p.values[i]);
          predicateString += ", ? ";
        }
        predicateString += " ) ";
      }
      return predicateString;
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
};

} // namespace rukh::orm
