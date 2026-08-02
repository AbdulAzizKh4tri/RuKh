#pragma once

#include <expected>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

#include <rukh/Exceptions.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/DbValue.hpp>
#include <rukh/db/Sqlite3Types.hpp>

namespace rukh::db {

class Sqlite3QueryExecutor {
public:
  static void bindQueryParam(sqlite3_stmt *stmt, int index, const std::vector<DbValue> &params) {
    if (std::holds_alternative<bool>(params[index])) {
      sqlite3_bind_int64(stmt, index + 1, (std::get<bool>(params[index])) ? 1 : 0);
    } else if (std::holds_alternative<int64_t>(params[index])) {
      sqlite3_bind_int64(stmt, index + 1, std::get<int64_t>(params[index]));
    } else if (std::holds_alternative<double>(params[index])) {
      sqlite3_bind_double(stmt, index + 1, std::get<double>(params[index]));
    } else if (std::holds_alternative<std::string>(params[index])) {
      sqlite3_bind_text(stmt, index + 1, std::get<std::string>(params[index]).c_str(), -1, SQLITE_STATIC);
    } else if (std::holds_alternative<std::vector<unsigned char>>(params[index])) {
      sqlite3_bind_blob(stmt, index + 1, std::get<std::vector<unsigned char>>(params[index]).data(),
                        std::get<std::vector<unsigned char>>(params[index]).size(), SQLITE_STATIC);
    } else {
      sqlite3_bind_null(stmt, index + 1);
    }
  }

  static std::expected<QueryResult, DatabaseError> executeOnConnection(Connection *conn, const std::string &sql,
                                                                       const std::vector<DbValue> &params = {}) {

    sqlite3 *dbConnection = conn->dbConnection;
    std::unordered_map<std::string, sqlite3_stmt *> &statements = conn->statements;

    sqlite3_stmt *stmt;
    if (statements.contains(sql)) {
      stmt = statements[sql];
    } else {
      int rc = sqlite3_prepare_v3(dbConnection, sql.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &stmt, nullptr);
      if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(dbConnection);
        SPDLOG_ERROR("SQL error: {}", err);
        return std::unexpected(DatabaseError{DbErrorType::QUERY_ERROR, err});
      }
      statements[sql] = stmt;
    }
    StatementResetGuard statementGuard(stmt);

    for (int i = 0; i < (int)params.size(); i++)
      bindQueryParam(stmt, i, params);

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    const char *rawSQL = sqlite3_expanded_sql(stmt);
    if (rawSQL) {
      SPDLOG_TRACE("SQL: {}", rawSQL);
      sqlite3_free((void *)rawSQL); // must free it
    }
#endif

    QueryResult result;
    result.columns = std::make_shared<std::unordered_map<std::string, size_t, StringHash, std::equal_to<>>>();
    bool first = true;

    bool queryComplete = false;
    while (not queryComplete) {
      int rc = sqlite3_step(stmt);
      switch (rc) {
      case SQLITE_ROW: {
        Row row;
        int colCount = sqlite3_column_count(stmt);
        for (int i = 0; i < colCount; i++) {
          if (first) {
            const char *name = sqlite3_column_name(stmt, i);
            (*result.columns)[name ? name : ""] = i;
          }
          switch (sqlite3_column_type(stmt, i)) {
          case SQLITE_INTEGER:
            row.values.push_back(sqlite3_column_int64(stmt, i));
            break;
          case SQLITE_FLOAT:
            row.values.push_back(sqlite3_column_double(stmt, i));
            break;
          case SQLITE_TEXT: {
            const unsigned char *text = sqlite3_column_text(stmt, i);
            row.values.push_back(text ? reinterpret_cast<const char *>(text) : "");
          } break;
          case SQLITE_BLOB: {
            const void *blob = sqlite3_column_blob(stmt, i);
            int size = sqlite3_column_bytes(stmt, i);
            std::vector<unsigned char> data;
            if (blob && size > 0) {
              const unsigned char *p = reinterpret_cast<const unsigned char *>(blob);
              data.assign(p, p + size);
            }
            row.values.push_back(data);
          } break;
          case SQLITE_NULL:
            row.values.push_back(nullptr);
            break;
          }
        }
        row.columns = result.columns;
        result.rows.push_back(std::move(row));
        first = false;
        break;
      }

      case SQLITE_DONE:
        queryComplete = true;
        break;

      case SQLITE_BUSY:
      case SQLITE_LOCKED:
        return std::unexpected(DatabaseError{DbErrorType::DB_BUSY, "Database is busy"});

      case SQLITE_CONSTRAINT: {
        int extended = sqlite3_extended_errcode(dbConnection);
        std::string err = sqlite3_errmsg(dbConnection);
        if (extended == SQLITE_CONSTRAINT_UNIQUE || extended == SQLITE_CONSTRAINT_PRIMARYKEY)
          return std::unexpected(DatabaseError{DbErrorType::DUPLICATE_KEY, err});
        return std::unexpected(DatabaseError{DbErrorType::CONSTRAINT_VIOLATION, err});
      }

      case SQLITE_TOOBIG:
        return std::unexpected(DatabaseError{DbErrorType::OTHER, sqlite3_errmsg(dbConnection)});

      default:
        throw DatabaseException("SQL error: " + std::string(sqlite3_errmsg(dbConnection)));
      }
    }

    result.affectedRows = sqlite3_changes(dbConnection);
    return result;
  }
};
} // namespace rukh::db
