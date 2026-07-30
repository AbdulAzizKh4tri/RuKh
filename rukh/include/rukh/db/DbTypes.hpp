#pragma once

#include <nlohmann/json.hpp>
#include <rukh/TypeHelpers.hpp>
#include <string>

namespace rukh::db {

using DbValue = std::variant<int64_t, double, std::string, std::vector<unsigned char>, std::nullptr_t>;

template <std::integral T>
  requires(!std::same_as<T, bool>)
DbValue toDbValueImpl(T v) {
  return static_cast<int64_t>(v);
}

template <typename T> DbValue toDbValueImpl(const std::optional<T> &v) {
  return v ? toDbValueImpl(*v) : DbValue{nullptr};
}

inline DbValue toDbValueImpl(DbValue v) { return v; }
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
      return std::nullopt;
    auto *val = std::get_if<T>(&values[it->second]);
    return val ? std::make_optional(*val) : std::nullopt;
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
