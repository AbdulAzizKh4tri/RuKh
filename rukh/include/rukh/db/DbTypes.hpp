#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbValue.hpp>

namespace rukh::db {

struct StringHash {
  // NOTE: Don't know what this does, but it works

  // TODO: understand it
  using is_transparent = void; // opts into heterogeneous lookup
  std::size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
};

class Row {
public:
  template <typename T> std::optional<T> as(const std::string_view col) const {
    auto it = columns->find(col);
    if (it == columns->end())
      return std::nullopt;                         // missing column
    return db::fromDbValue<T>(values[it->second]); // throws on type mismatch or bad range
  }

  template <typename T> std::optional<T> as(const size_t col) const {
    auto *val = std::get_if<T>(&values[col]);
    return val ? std::make_optional(*val) : std::nullopt;
  }

  bool isNull(std::string_view col) const {
    auto it = columns->find(col);
    if (it == columns->end())
      throw std::out_of_range("no such column: " + std::string(col));
    return std::holds_alternative<std::nullptr_t>(values[it->second]);
  }

  DbValue operator[](std::string_view column) const {
    auto it = columns->find(column);
    if (it == columns->end())
      throw std::out_of_range("no such column: " + std::string(column));
    return values[it->second];
  }

  DbValue operator[](const size_t index) const { return values[index]; }

  std::vector<DbValue> values;
  std::shared_ptr<std::unordered_map<std::string, size_t, StringHash, std::equal_to<>>> columns;

  std::string toString() const {
    std::string s;
    for (size_t i = 0; i < values.size(); i++) {
      s += dbValueToString(values[i]) + " ";
    }
    return s;
  }
};

struct QueryResult {
  size_t affectedRows; // for non SELECT queries
  std::vector<Row> rows;
  std::shared_ptr<std::unordered_map<std::string, size_t, StringHash, std::equal_to<>>> columns;

  QueryResult &operator+=(const QueryResult &other) {
    affectedRows += other.affectedRows;
    rows.insert(rows.end(), other.rows.begin(), other.rows.end());
    return *this;
  }
};

enum class DbErrorType {
  CONNECTION_FAILED,
  CONSTRAINT_VIOLATION,
  DB_BUSY,
  DUPLICATE_KEY,
  FOREIGN_KEY_VIOLATION,
  INVALID_COLUMN,
  OTHER,
  TOO_BIG,
  TRANSACTION_ENDED,
  TRANSACTION_ERROR,
  TRIGGER_ERROR,
  QUERY_ERROR,
  UNIQUE_CONSTRAINT_VIOLATION
};

struct DatabaseError {
  DbErrorType type;
  std::string message;
};

} // namespace rukh::db
