/**
 * @file DbTypes.hpp
 * @brief Types used for IDatabase and ITransaction implementations.
 */
#pragma once

#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbValue.hpp>
#include <rukh/utils.hpp>

#include <spdlog/spdlog.h>

namespace rukh::db {

/**
 * @brief Represents a row of a query result
 *
 * @see `DbValue`
 * @see `fromDbValue`
 * @see `toDbValue`
 */
class Row {
public:
  /**
   * @brief Get a column value as a specific type
   * @tparam T The type to try to cast the value to
   * @param col The column name
   * @returns The value as type T
   * @throws DatabaseException if the column does not exist.
   * @throws DatabaseException if the value is not of type T and can't be derived from type T using db::fromDbValue.
   *
   * @see db::fromDbValue
   */
  template <typename T> T as(const std::string_view col) const {
    auto it = columns->find(col);
    if (it == columns->end())
      throw DatabaseException("No such column: " + std::string(col));
    return db::fromDbValue<T>(values[it->second]); // throws on type mismatch
  }

  /// same as @ref as(const std::string_view) const "Row::as(string_view)" but uses the column index.
  template <typename T> T as(const size_t colIndex) const {
    return db::fromDbValue<T>(values[colIndex]); // throws on type mismatch
  }

  /// Checks if a column with the given name exists
  bool columnExists(std::string_view col) const { return columns->find(col) != columns->end(); }

  /**
   * @brief Checks if a column with the given name is null
   * @throws DatabaseException if the column does not exist
   */
  bool isNull(std::string_view col) const {
    auto it = columns->find(col);
    if (it == columns->end())
      throw DatabaseException("No such column: " + std::string(col));
    return std::holds_alternative<std::nullptr_t>(values[it->second]);
  }

  /// same as isNull(std::string_view col) but uses the column index
  bool isNull(const size_t colIndex) const { return std::holds_alternative<std::nullptr_t>(values[colIndex]); }

  /// Get a column value directly as a DbValue using column name
  DbValue operator[](std::string_view col) const {
    auto it = columns->find(col);
    if (it == columns->end())
      throw DatabaseException("No such column: " + std::string(col));
    return values[it->second];
  }

  /// Get a column value directly as a DbValue using column index
  DbValue operator[](const size_t colIndex) const { return values[colIndex]; }

  /// The actual row returned from the table.
  std::vector<DbValue> values;

  /**
   * @brief A map of column name to column index.
   * `std::shared_ptr` because it's created once and used by every `Row` in a `QueryResult`.
   */
  std::shared_ptr<std::unordered_map<std::string, size_t, StringHash, std::equal_to<>>> columns;

  /// Returns a string representation of the row `values`.
  std::string toString() const {
    constexpr int COL_WIDTH = 16;
    std::ostringstream ss;
    for (size_t i = 0; i < values.size(); i++) {
      ss << std::left << std::setw(COL_WIDTH) << dbValueToString(values[i]);
    }
    return ss.str();
  }
};

/// A collection of Rows from a query with column names and number of affectedRows.
struct QueryResult {
  size_t affectedRows; ///< for non SELECT queries

  std::vector<Row> rows;

  /**
   * @brief A map of column name to column index.
   * `std::shared_ptr` because it's created once and used by every `Row` in `QueryResult` .
   */
  std::shared_ptr<std::unordered_map<std::string, size_t, StringHash, std::equal_to<>>> columns;

  size_t size() const { return rows.size(); }

  Row operator[](const size_t index) const { return rows[index]; }

  /// Returns a string representation of the query result with column names at the top.
  std::string toString() const {
    std::ostringstream ss;
    constexpr int COL_WIDTH = 16;

    std::vector<std::pair<std::string, int>> cols(columns->begin(), columns->end());
    std::sort(cols.begin(), cols.end(), [](const auto &a, const auto &b) { return a.second < b.second; });

    ss << "\n";
    for (auto &[key, _] : cols) {
      ss << std::left << std::setw(COL_WIDTH) << key;
    }
    ss << "\n";
    for (size_t i = 0; i < rows.size(); i++) {
      ss << rows[i].toString() << "\n";
    }
    return ss.str();
  }
};

/// Database error types
enum class DbErrorType {
  CONNECTION_FAILED,
  CONSTRAINT_VIOLATION,
  DB_BUSY,
  DUPLICATE_KEY,
  FOREIGN_KEY_VIOLATION,
  INVALID_COLUMN,
  NOT_IMPLEMENTED,
  OTHER,
  TOO_BIG,
  TRANSACTION_ENDED,
  TRANSACTION_ERROR,
  TRIGGER_ERROR,
  QUERY_ERROR,
  UNIQUE_CONSTRAINT_VIOLATION
};

/// Database error
struct DatabaseError {
  DbErrorType type;
  std::string message;
};

} // namespace rukh::db
