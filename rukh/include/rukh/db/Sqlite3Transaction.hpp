#pragma once

#include <memory>
#include <rukh/Exceptions.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/db/Sqlite3QueryExecutor.hpp>
#include <rukh/db/Sqlite3Types.hpp>
#include <spdlog/spdlog.h>

namespace rukh::db {
// TODO: handle exec() errors

class Sqlite3Transaction : public ITransaction {
public:
  static std::expected<std::unique_ptr<Sqlite3Transaction>, DatabaseError> createTransaction(Connection *conn) {
    try {
      Sqlite3Transaction *t = new Sqlite3Transaction(conn);
      return std::unique_ptr<Sqlite3Transaction>(t);
    } catch (DatabaseException &e) {
      return std::unexpected(DatabaseError{DbErrorType::TRANSACTION_ERROR, e.what()});
    }
  }

  std::expected<QueryResult, DatabaseError> executeQuery(const std::string &sql,
                                                         const std::vector<DbValue> params = {}) override {
    if (isTransactionEnded())
      throw DatabaseException("Transaction already ended");
    TransactionLockGuard guard{this};
    return Sqlite3QueryExecutor::executeOnConnection(conn_, sql, params);
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

  Sqlite3Transaction(Connection *conn) : conn_(conn) {
    if (not begin("DEFERRED"))
      throw DatabaseException("Failed to begin transaction");
  }

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
};

} // namespace rukh::db
