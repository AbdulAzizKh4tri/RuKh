#pragma once

#include <rukh/Exceptions.hpp>
#include <rukh/ThreadPool.hpp>
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
  Sqlite3Transaction(Connection *conn, ThreadPool *threadPool, std::function<void()> abandonFn)
      : conn_(conn), threadPool_(threadPool), abandonFn_(abandonFn) {}

  Task<std::expected<QueryResult, DatabaseError>> executeQuery(const std::string &sql,
                                                               const std::vector<DbValue> params = {}) override {
    if (isTransactionEnded())
      throw DatabaseException("Transaction already ended");
    TransactionLockGuard guard{this};

    co_return co_await threadPool_->submit([&, this]() -> std::expected<db::QueryResult, db::DatabaseError> {
      return Sqlite3QueryExecutor::executeOnConnection(conn_, sql, params);
    });
  }

  Task<std::expected<QueryResult, DatabaseError>> begin(const std::string &mode) override {
    if (isTransactionEnded())
      throw DatabaseException("Transaction already ended");

    std::string sql = "BEGIN TRANSACTION " + mode + ";";
    TransactionLockGuard guard{this};

    co_return co_await threadPool_->submit([this, sql]() -> std::expected<QueryResult, DatabaseError> {
      return Sqlite3QueryExecutor::executeOnConnection(conn_, sql.c_str(), {});
    });
  }

  Task<std::expected<QueryResult, DatabaseError>> commit() override {
    if (isTransactionEnded()) {
      SPDLOG_WARN("Attempted to commit ended Transaction");
      co_return std::unexpected(DatabaseError{db::DbErrorType::TRANSACTION_ENDED});
    }
    TransactionLockGuard guard{this};

    auto result = co_await threadPool_->submit([this]() -> std::expected<QueryResult, DatabaseError> {
      return Sqlite3QueryExecutor::executeOnConnection(conn_, "COMMIT;", {});
    });

    if (result)
      ended_ = true;
    co_return result;
  }

  Task<std::expected<QueryResult, DatabaseError>> rollback() override {
    if (isTransactionEnded()) {
      SPDLOG_WARN("Attempted to rollback ended Transaction");
      co_return std::unexpected(DatabaseError{db::DbErrorType::TRANSACTION_ENDED});
    }
    TransactionLockGuard guard{this};

    auto result = co_await threadPool_->submit([this]() -> std::expected<QueryResult, DatabaseError> {
      return Sqlite3QueryExecutor::executeOnConnection(conn_, "ROLLBACK;", {});
    });

    if (result)
      ended_ = true;
    co_return result;
  }

  void abandon() override {
    if (isTransactionEnded()) {
      return;
    }
    TransactionLockGuard guard{this};
    abandonFn_();
    ended_ = true;
  }

  bool isTransactionEnded() const override { return ended_; }

  Connection *getConnection() { return conn_; }

private:
  Connection *conn_;
  std::function<void()> abandonFn_;
  ThreadPool *threadPool_;
  std::atomic<bool> busy_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool ended_ = false;

  struct TransactionLockGuard {
    Sqlite3Transaction *t;
    TransactionLockGuard(Sqlite3Transaction *t) : t(t) { t->acquireTransactionLock(); }
    ~TransactionLockGuard() { t->releaseTransactionLock(); }
  };

  void acquireTransactionLock() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return not busy_; });
    busy_ = true;
  }

  void releaseTransactionLock() {
    std::unique_lock<std::mutex> lock(mutex_);
    busy_ = false;
    cv_.notify_one();
  }
};

} // namespace rukh::db
