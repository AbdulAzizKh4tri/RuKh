/**
 * @file Sqlite3Transaction.hpp
 * @brief Sqlite3 implementation of ITransaction
 */
#pragma once

#include <functional>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/sqlite3/Sqlite3QueryExecutor.hpp>
#include <rukh/pool/ThreadPool.hpp>

namespace rukh::db {

/// Sqlite3 implementation of ITransaction
class Sqlite3Transaction : public ITransaction {
public:
  Sqlite3Transaction(Connection *conn, pool::ThreadPool *threadPool, std::move_only_function<void() noexcept> abandonFn)
      : conn_(conn), threadPool_(threadPool), abandonFn_(std::move(abandonFn)) {}

  core::Task<std::expected<QueryResult, DatabaseError>> executeQuery(const std::string &sql,
                                                                     const std::vector<DbValue> params = {}) override {
    TransactionLockGuard guard{this};
    if (isTransactionEnded())
      throw DatabaseException("Transaction already ended");

    co_return co_await threadPool_->submit([&, this]() -> std::expected<db::QueryResult, db::DatabaseError> {
      return Sqlite3QueryExecutor::executeOnConnection(conn_, sql, params);
    });
  }

  core::Task<std::expected<QueryResult, DatabaseError>> begin(const std::string &mode) override {
    TransactionLockGuard guard{this};
    if (isTransactionEnded())
      throw DatabaseException("Transaction already ended");

    std::string sql = "BEGIN TRANSACTION " + mode + ";";

    co_return co_await threadPool_->submit([this, sql]() -> std::expected<QueryResult, DatabaseError> {
      return Sqlite3QueryExecutor::executeOnConnection(conn_, sql.c_str(), {});
    });
  }

  core::Task<std::expected<QueryResult, DatabaseError>> commit() override {
    TransactionLockGuard guard{this};
    if (isTransactionEnded()) {
      SPDLOG_WARN("Attempted to commit ended Transaction");
      co_return std::unexpected(DatabaseError{db::DbErrorType::TRANSACTION_ENDED});
    }

    auto result = co_await threadPool_->submit([this]() -> std::expected<QueryResult, DatabaseError> {
      return Sqlite3QueryExecutor::executeOnConnection(conn_, "COMMIT;", {});
    });

    if (result)
      ended_ = true;
    co_return result;
  }

  core::Task<std::expected<QueryResult, DatabaseError>> rollback() override {
    TransactionLockGuard guard{this};
    if (isTransactionEnded()) {
      SPDLOG_WARN("Attempted to rollback ended Transaction");
      co_return std::unexpected(DatabaseError{db::DbErrorType::TRANSACTION_ENDED});
    }

    auto result = co_await threadPool_->submit([this]() -> std::expected<QueryResult, DatabaseError> {
      return Sqlite3QueryExecutor::executeOnConnection(conn_, "ROLLBACK;", {});
    });

    if (result)
      ended_ = true;
    co_return result;
  }

  void abandon() override {
    TransactionLockGuard guard{this};
    if (isTransactionEnded())
      return;
    threadPool_->fireAndForget(std::move(abandonFn_));
    ended_ = true;
  }

  ~Sqlite3Transaction() { abandon(); }

  bool isTransactionEnded() const override { return ended_; }

  /// @internalMethod
  Connection *getConnection() { return conn_; }

private:
  Connection *conn_;
  std::move_only_function<void() noexcept> abandonFn_;
  pool::ThreadPool *threadPool_;
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
