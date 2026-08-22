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
 * @note The variant currently maps to values needed for sqlite3, may change later if postGres support is added.
 */
using DbValue = std::variant<int64_t, double, std::string, bool, std::vector<unsigned char>, std::nullptr_t>;

/// @internalGroup @{

/// @internalMethod
template <typename T>
  requires(std::same_as<T, int64_t> || std::same_as<T, double> || std::same_as<T, std::string> ||
           std::same_as<T, bool> || std::same_as<T, std::vector<unsigned char>> || std::same_as<T, std::nullptr_t>)
DbValue toDbValueImpl(T v) {
  return v;
}

/// @internalMethod
template <std::integral T>
  requires(not std::same_as<T, bool> && not std::same_as<T, int64_t>)
DbValue toDbValueImpl(T v) {
  return static_cast<int64_t>(v);
}

/// @internalMethod
template <std::floating_point T>
  requires(not std::same_as<T, double>)
DbValue toDbValueImpl(T v) {
  return static_cast<double>(v);
}

/// @internalMethod
template <typename T> DbValue toDbValueImpl(const std::optional<T> &v) {
  return v ? toDbValueImpl(*v) : DbValue{nullptr};
}

inline DbValue toDbValueImpl(DbValue v) { return v; }                      ///< @internalMethod
inline DbValue toDbValueImpl(const char *v) { return std::string(v); }     ///< @internalMethod
inline DbValue toDbValueImpl(const nlohmann::json &v) { return v.dump(); } ///< @internalMethod

/// @internalMethod
inline DbValue toDbValueImpl(std::chrono::sys_seconds tp) {
  return static_cast<int64_t>(tp.time_since_epoch().count());
}

/// @}

//=========================================================================================

/// @internalGroup @{

/// @internalMethod
template <typename T>
  requires(std::same_as<T, int64_t> || std::same_as<T, double> || std::same_as<T, std::string> ||
           std::same_as<T, bool> || std::same_as<T, std::vector<unsigned char>> || std::same_as<T, std::nullptr_t>)
T fromDbValueImpl(const DbValue &v) {
  const auto *raw = std::get_if<T>(&v);

  if (not raw)
    throw DatabaseException("DB value does not hold expected alternative for type " + std::string(typeid(T).name()));

  return *raw;
}

/// @internalMethod
template <std::integral T>
  requires(not std::same_as<T, bool> && not std::same_as<T, int64_t>)
T fromDbValueImpl(const DbValue &v) {
  const auto *raw = std::get_if<int64_t>(&v);

  if (not raw)
    throw DatabaseException("DB value does not hold int64_t for type " + std::string(typeid(T).name()));

  if (not std::in_range<T>(*raw))
    throw DatabaseException("DB value " + std::to_string(*raw) + " out of range for target type " +
                            std::string(typeid(T).name()));

  return static_cast<T>(*raw);
}

/// @internalMethod
template <std::floating_point T>
  requires(not std::same_as<T, double>)
T fromDbValueImpl(const DbValue &v) {
  const auto *raw = std::get_if<double>(&v);

  if (not raw)
    throw DatabaseException("DB value does not hold double for type " + std::string(typeid(T).name()));

  if (*raw > 0 && *raw > static_cast<double>(std::numeric_limits<T>::max()))
    throw DatabaseException("DB value out of range for target type " + std::string(typeid(T).name()));

  if (*raw < 0 && *raw < static_cast<double>(std::numeric_limits<T>::lowest()))
    throw DatabaseException("DB value out of range for target type " + std::string(typeid(T).name()));

  return static_cast<T>(*raw);
}

/// @}

/**
 * @brief Converts a value `v` of type `T` to a `DbValue`. Useful for custom Model field types.
 *
 * nullptr if T is std::nullopt_t
 *
 * For more info about custom types, see @ref fromDbValueImpl
 */
template <typename T> DbValue toDbValue(const T &v) {
  if constexpr (OptionalT<T>) {
    return v.has_value() ? toDbValueImpl(*v) : DbValue{nullptr};
  } else {
    return toDbValueImpl(v);
  }
}

/**
 * @brief This is what allows us to have custom types in models.
 *
 *  Example of a custom type:
 *  @code
 *  namespace random_namespace {
 *
 *		 struct Email {
 *		   std::string email;
 *		 };
 *
 *		 inline rukh::db::DbValue toDbValueImpl(random_namespace::Email e) { return e.email; }
 *
 *		 inline void to_json(nlohmann::json &j, const Email &email) { j = email.email; }
 *
 *  }
 *
 *  template <> inline random_namespace::Email rukh::db::fromDbValueImpl<random_namespace::Email>(const DbValue &raw) {
 *    return random_namespace::Email{std::get<std::string>(raw)};
 *  }
 *  @endcode
 *
 *  Email can now be used as a type in models.
 *
 * @attention `fromDbValueImpl` must be in rukh::db namespace. toDbValueImpl can be in `random_namespace`.
 *
 * @note The to_json function is only required if the custom type is to be json serialized.
 *
 * @see `toDbValue`
 * @see `fromDbValue`
 */
template <typename T> T fromDbValueImpl(const DbValue &) {
  static_assert(always_false_v<T>, "No DbValue conversion defined for this type");
}

/**
 * @brief Converts a `DbValue` to a value of type `T`. Useful for custom model field types.
 *
 * For more info about custom types, see @ref fromDbValueImpl
 */
template <typename T> T fromDbValue(const DbValue &v) { return fromDbValueImpl<T>(v); }

/// Converts a `DbValue` to a string
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
