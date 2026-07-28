#pragma once

#include <rukh/Exceptions.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/db/Sqlite3Db.hpp>
#include <rukh/db/Sqlite3Types.hpp>
#include <spdlog/spdlog.h>

namespace rukh::db {
// TODO: handle exec() errors

class Sqlite3Transaction : public ITransaction {
public:
  Sqlite3Transaction(Connection *conn) : conn_(conn) {
    if (not begin("DEFERRED"))
      throw DatabaseException("Failed to begin transaction");
  }

  std::expected<QueryResult, DatabaseError> executeQuery(const std::string &sql,
                                                         const std::vector<DbValue> params = {}) override {
    if (isTransactionEnded())
      throw DatabaseException("Transaction already ended");

    TransactionLockGuard guard{this};
    sqlite3 *dbConnection = conn_->dbConnection;
    std::unordered_map<std::string, sqlite3_stmt *> &statements = conn_->statements;

    sqlite3_stmt *stmt;
    if (statements.contains(sql)) {
      stmt = statements[sql];
    } else {
      int rc = sqlite3_prepare_v3(dbConnection, sql.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &stmt, nullptr);
      if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(dbConnection);
        SPDLOG_ERROR("SQL error: {}", err);
        return std::unexpected(DatabaseError{DatabaseError::ErrorType::QUERY_ERROR, err});
      }
      statements[sql] = stmt;
    }
    StatementResetGuard statementGuard(stmt);

    for (int i = 0; i < (int)params.size(); i++)
      bindQueryParam(stmt, i, params);

    QueryResult result;
    result.columns = std::make_shared<std::unordered_map<std::string, size_t>>();
    bool first = true;

    while (true) {
      int rc = sqlite3_step(stmt);
      if (rc == SQLITE_ROW) {
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
      } else if (rc == SQLITE_DONE) {
        break;
      } else if (rc == SQLITE_BUSY) {
        return std::unexpected(DatabaseError{DatabaseError::ErrorType::DB_BUSY, "Database is busy"});
      } else {
        throw DatabaseException("SQL error: " + std::string(sqlite3_errmsg(dbConnection)));
      }
    }

    result.affectedRows = sqlite3_changes(dbConnection);
    return result;
  }

  bool begin(const std::string &mode) override {
    if (isTransactionEnded())
      throw DatabaseException("Transaction already ended");

    std::string sql = "BEGIN TRANSACTION " + mode + ";";
    TransactionLockGuard guard{this};
    char *errMsg = nullptr;
    int rc = sqlite3_exec(conn_->dbConnection, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
      SPDLOG_ERROR("SQLite error ({}): {}", rc, errMsg ? errMsg : "Unknown error");
      sqlite3_free(errMsg);
      return false;
    }
    return true;
  }

  bool commit() override {
    if (isTransactionEnded()) {
      SPDLOG_WARN("Attempted to commit ended Transaction");
      return false;
    }
    TransactionLockGuard guard{this};
    char *errMsg = nullptr;
    int rc = sqlite3_exec(conn_->dbConnection, "COMMIT;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
      SPDLOG_ERROR("SQLite error ({}): {}", rc, errMsg ? errMsg : "Unknown error");
      sqlite3_free(errMsg);
      return false;
    }
    ended_ = true;
    return true;
  }

  bool rollback() override {
    if (isTransactionEnded()) {
      SPDLOG_WARN("Attempted to rollback ended Transaction");
      return false;
    }
    TransactionLockGuard guard{this};
    char *errMsg = nullptr;
    int rc = sqlite3_exec(conn_->dbConnection, "ROLLBACK;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
      SPDLOG_ERROR("SQLite error ({}): {}", rc, errMsg ? errMsg : "Unknown error");
      sqlite3_free(errMsg);
      return false;
    }
    ended_ = true;
    return true;
  }

  bool isTransactionEnded() const override { return ended_; }

  Connection *getConnection() { return conn_; }

private:
  Connection *conn_;
  std::atomic<bool> busy_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool ended_ = false;

  struct TransactionLockGuard {
    Sqlite3Transaction *t;
    TransactionLockGuard(Sqlite3Transaction *t) : t(t) { t->acquireTransaction(); }
    ~TransactionLockGuard() { t->releaseTransaction(); }
  };

  void acquireTransaction() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return not busy_; });
    busy_ = true;
  }

  void releaseTransaction() {
    std::unique_lock<std::mutex> lock(mutex_);
    busy_ = false;
    cv_.notify_one();
  }

  void bindQueryParam(sqlite3_stmt *stmt, int index, const std::vector<DbValue> &params) {
    if (std::holds_alternative<int64_t>(params[index])) {
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
};

} // namespace rukh::db
