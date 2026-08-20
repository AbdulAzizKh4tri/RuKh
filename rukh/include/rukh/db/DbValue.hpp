/**
 * @file DbValue.hpp
 * @brief Represents a value that can be stored in the database.
 *
 * Every value that needs to be stored in the database must be either a DbValue or be convertible to one.
 */
#pragma once

#include <nlohmann/json.hpp>

#include <rukh/Exceptions.hpp>
#include <rukh/TypeHelpers.hpp>

namespace rukh::db {

/**
 * @brief Represents a value that can be stored in the database.
 *
 * Every value that needs to be stored in the database must be either a DbValue or be convertible to one.
 */
using DbValue = std::variant<int64_t, double, std::string, bool, std::vector<unsigned char>, std::nullptr_t>;

template <std::integral T>
  requires(not std::same_as<T, bool>)
DbValue toDbValueImpl(T v) {
  return static_cast<int64_t>(v);
}

template <typename T> DbValue toDbValueImpl(const std::optional<T> &v) {
  return v ? toDbValueImpl(*v) : DbValue{nullptr};
}

inline DbValue toDbValueImpl(DbValue v) { return v; }
inline DbValue toDbValueImpl(bool v) { return v; }
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

// No definition — only declared. Accessing ::type on an unspecialized T is ill-formed,
// which is exactly what lets HasDbValueSource correctly reject unsupported types.
template <typename T> struct DbValueSource;

// Identity: T is one of DbValue's actual alternatives.
template <typename T>
  requires(std::same_as<T, int64_t> or std::same_as<T, double> or std::same_as<T, std::string> or
           std::same_as<T, bool> or std::same_as<T, std::vector<unsigned char>> or std::same_as<T, std::nullptr_t>)
struct DbValueSource<T> {
  using type = T;
};

// Any integral other than bool/int64_t is read out of the int64_t alternative.
template <std::integral T>
  requires(not std::same_as<T, bool> and not std::same_as<T, int64_t>)
struct DbValueSource<T> {
  using type = int64_t;
};

// Any floating type other than double is read out of the double alternative.
template <std::floating_point T>
  requires(not std::same_as<T, double>)
struct DbValueSource<T> {
  using type = double;
};

template <typename T>
concept HasDbValueSource = requires { typename DbValueSource<T>::type; };

// Generic conversion body: identity, or range-checked narrowing. Custom types (e.g. Uuid)
// provide their own full specialization of this template next to the type itself.
template <typename T> T fromDbValueImpl(const typename DbValueSource<T>::type &raw) {
  using Source = typename DbValueSource<T>::type;

  if constexpr (std::same_as<T, Source>) {
    return raw;
  } else if constexpr (std::integral<T> and std::integral<Source>) {
    if (not std::in_range<T>(raw))
      throw DatabaseException("DB value " + std::to_string(raw) + " out of range for target type " +
                              std::string(typeid(T).name()));
    return static_cast<T>(raw);
  } else if constexpr (std::floating_point<T> and std::floating_point<Source>) {
    if (raw > 0 and raw > static_cast<Source>(std::numeric_limits<T>::max()))
      throw DatabaseException("DB value out of range for target type " + std::string(typeid(T).name()));
    if (raw < 0 and raw < static_cast<Source>(std::numeric_limits<T>::lowest()))
      throw DatabaseException("DB value out of range for target type " + std::string(typeid(T).name()));
    return static_cast<T>(raw);
  } else {
    static_assert(always_false_v<T>, "fromDbValueImpl has no generic conversion for this T/Source pair — "
                                     "provide an explicit specialization for your custom type.");
  }
}

template <typename T>
  requires HasDbValueSource<T>
T fromDbValue(const DbValue &v) {
  using Source = typename DbValueSource<T>::type;
  auto *raw = std::get_if<Source>(&v);
  if (not raw)
    throw DatabaseException("DbValue does not hold expected alternative for type " + std::string(typeid(T).name()));
  return fromDbValueImpl<T>(*raw);
}

inline std::string dbValueToString(const DbValue &v) {
  if (std::holds_alternative<std::string>(v))
    return std::get<std::string>(v);

  if (std::holds_alternative<int64_t>(v))
    return std::to_string(std::get<int64_t>(v));

  if (std::holds_alternative<double>(v))
    return std::to_string(std::get<double>(v));

  if (std::holds_alternative<std::vector<unsigned char>>(v))
    return "BLOB_" + std::to_string(std::get<std::vector<unsigned char>>(v).size());

  if (std::holds_alternative<bool>(v))
    return std::get<bool>(v) ? "TRUE" : "FALSE";

  return {};
}

} // namespace rukh::db
