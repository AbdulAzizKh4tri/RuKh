/**
 * @file Sqlite3Db.hpp
 * @brief Sqlite3 implementation of IDatabase
 */
#pragma once

#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <string>

#include <rukh/Exceptions.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/db/sqlite3/Sqlite3QueryExecutor.hpp>
#include <rukh/db/sqlite3/Sqlite3Transaction.hpp>
#include <rukh/db/sqlite3/Sqlite3Types.hpp>
#include <rukh/pool/ThreadPool.hpp>

namespace rukh::db {
/// ms to wait on SQLITE_BUSY instead of failing immediately
/// \todo maybe make it configurable
const int SQLITE3_BUSY_TIMEOUT = 5000;

/// Sqlite3 implementation of IDatabase
class Sqlite3Db : public IDatabase {
public:
  /**
   * @brief Constructor
   * @param filename Path to database file
   * @param threadPool
   * @param ConnectionPoolSize Number of connections to sqlite to keep in ConnectionQueue
   *
   * Enables foreign keys and WAL mode
   *
   * @throws DatabaseException if unable to open database or unable to configure connections correctly.
   */
  Sqlite3Db(const std::string &filename, pool::ThreadPool *threadPool, size_t ConnectionPoolSize = 4)
      : threadPool_(threadPool) {
    for (int i = 0; i < ConnectionPoolSize; i++) {
      sqlite3 *dbConnection;
      int rc = sqlite3_open_v2(filename.c_str(), &dbConnection, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
      if (rc) {
        if (dbConnection)
          sqlite3_close(dbConnection);
        throw DatabaseException("Can't open database");
      }

      sqlite3_busy_timeout(dbConnection, SQLITE3_BUSY_TIMEOUT);

      rc = sqlite3_exec(dbConnection, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
      if (rc != SQLITE_OK) {
        sqlite3_close(dbConnection);
        throw DatabaseException("Failed to enable foreign keys");
      }

      auto conn = new Connection();
      conn->dbConnection = dbConnection;
      connectionQueue_.addConnection(conn);
    }

    Connection *conn = acquireConnection();
    int rc = sqlite3_exec(conn->dbConnection, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
      throw DatabaseException("Failed to enable WAL mode");
    }
    releaseConnection(conn);
  }

  /// Acquires a connection from ConnectionQueue and executes query on it. See @ref Sqlite3QueryExecutor
  core::Task<std::expected<QueryResult, DatabaseError>> executeQuery(const std::string &sql,
                                                                     const std::vector<DbValue> &params = {}) override {
    Connection *conn = acquireConnection();
    ConnectionReleaseGuard connectionGuard{conn, &connectionQueue_};

    co_return co_await threadPool_->submit([&, this]() -> std::expected<db::QueryResult, db::DatabaseError> {
      return Sqlite3QueryExecutor::executeOnConnection(conn, sql, params);
    });
  }

  std::expected<std::unique_ptr<ITransaction>, DatabaseError> acquireTransaction() override {
    Connection *conn = acquireConnection();

    auto abandonFn = [this, conn] noexcept {
      char *errMsg = nullptr;
      sqlite3_exec(conn->dbConnection, "ROLLBACK;", nullptr, nullptr, &errMsg);
      if (errMsg) {
        SPDLOG_CRITICAL("Failed to rollback transaction with abadonFn: {}", errMsg);
        sqlite3_free(errMsg);
      }
      releaseConnection(conn);
    };

    auto t = std::make_unique<Sqlite3Transaction>(conn, threadPool_, abandonFn);

    return std::move(t);
  };

  void releaseTransaction(ITransaction *transaction) override {
    if (transaction == nullptr) {
      SPDLOG_WARN("Attempted to end null Transaction");
      return;
    }

    Sqlite3Transaction *t = dynamic_cast<Sqlite3Transaction *>(transaction);

    if (t == nullptr) {
      SPDLOG_CRITICAL("Sqlite3Db::endTransaction() called with a Transaction not created by Sqlite3Db");
      return;
    }

    releaseConnection(t->getConnection());
  }

private:
  Connection *acquireConnection() { return connectionQueue_.acquire(); }
  void releaseConnection(Connection *conn) { connectionQueue_.release(conn); }

  ConnectionQueue connectionQueue_;
  pool::ThreadPool *threadPool_;
};

} // namespace rukh::db
