#pragma once

#include <nlohmann/json.hpp>
#include <rukh/concepts.hpp>
#include <string>

namespace rukh::db {

using DbValue = std::variant<int64_t, double, std::string, std::vector<unsigned char>, std::nullptr_t>;

template <std::integral T>
  requires(!std::same_as<T, bool>)
DbValue toDbValueImpl(T v) {
  return static_cast<int64_t>(v);
}

inline DbValue toDbValueImpl(bool v) { return static_cast<int64_t>(v); }
inline DbValue toDbValueImpl(float v) { return static_cast<double>(v); }
inline DbValue toDbValueImpl(double v) { return v; }
inline DbValue toDbValueImpl(const std::string &v) { return v; }
inline DbValue toDbValueImpl(const char *v) { return std::string(v); }
inline DbValue toDbValueImpl(const std::vector<unsigned char> &v) { return v; }
inline DbValue toDbValueImpl(const nlohmann::json &v) { return v.dump(); }
inline DbValue toDbValueImpl(std::chrono::sys_seconds tp) {
  return static_cast<int64_t>(tp.time_since_epoch().count());
}
inline DbValue toDbValueImpl(std::nullptr_t) { return nullptr; }

template <typename T> DbValue toDbValue(const T &v) {
  if constexpr (OptionalT<T>) {
    return v.has_value() ? toDbValueImpl(*v) : DbValue{nullptr};
  } else {
    return toDbValueImpl(v);
  }
}

inline std::string dbValueToString(const DbValue &v) {
  if (std::holds_alternative<std::string>(v))
    return std::get<std::string>(v);

  if (std::holds_alternative<int64_t>(v))
    return std::to_string(std::get<int64_t>(v));

  if (std::holds_alternative<double>(v))
    return std::to_string(std::get<double>(v));

  return {};
}

class Row {
public:
  template <typename T> std::optional<T> as(const std::string &col) const {
    auto it = columns->find(col);
    if (it == columns->end())
      return std::nullopt;
    auto *val = std::get_if<T>(&values[it->second]);
    return val ? std::make_optional(*val) : std::nullopt;
  }

  template <typename T> std::optional<T> as(const size_t col) const {
    auto *val = std::get_if<T>(&values[col]);
    return val ? std::make_optional(*val) : std::nullopt;
  }

  bool isNull(const std::string &col) const {
    return std::holds_alternative<std::nullptr_t>(values[(*columns).at(col)]);
  }

  DbValue operator[](const std::string &column) { return values[(*columns).at(column)]; }
  DbValue operator[](const size_t index) { return values[index]; }

  std::vector<DbValue> values;
  std::shared_ptr<std::unordered_map<std::string, size_t>> columns;

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
  std::shared_ptr<std::unordered_map<std::string, size_t>> columns;

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
  INVALID_COLUMN,
  OTHER,
  TOO_BIG,
  TRANSACTION_ENDED,
  TRANSACTION_ERROR,
  QUERY_ERROR
};

struct DatabaseError {
  DbErrorType type;
  std::string message;
};

} // namespace rukh::db
