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

    std::vector<Connection *> conns;
    conns.reserve(ConnectionPoolSize);

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

      if (i == 0) {
        rc = sqlite3_exec(dbConnection, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
          sqlite3_close(dbConnection);
          throw DatabaseException("Failed to enable WAL mode");
        }
      }

      connections_.push_back(std::make_unique<Connection>());
      connections_.back()->dbConnection = dbConnection;
      conns.push_back(connections_.back().get());
    }

    connectionPool_ = std::make_unique<core::AsyncPool<Connection>>(std::move(conns));
  }

  /**
   * @brief Finalize cached statements and close every connection. AsyncPool doesn't
   * own connection lifecycle — that ownership lives here now.
   */
  ~Sqlite3Db() {
    for (auto &conn : connections_) {
      for (auto &[_, statement] : conn->statements)
        sqlite3_finalize(statement);
      sqlite3_close(conn->dbConnection);
    }
  }

  /// Acquires a connection from ConnectionQueue and executes query on it. See @ref Sqlite3QueryExecutor
  core::Task<std::expected<QueryResult, DatabaseError>> executeQuery(const std::string &sql,
                                                                     const std::vector<DbValue> &params = {}) override {
    Connection *conn = co_await connectionPool_->acquire();
    ConnectionReleaseGuard connectionGuard{conn, connectionPool_.get()};

    co_return co_await threadPool_->submit([&, this]() -> std::expected<db::QueryResult, db::DatabaseError> {
      return Sqlite3QueryExecutor::executeOnConnection(conn, sql, params);
    });
  }

  core::Task<std::expected<std::unique_ptr<ITransaction>, DatabaseError>> acquireTransaction() override {
    Connection *conn = co_await connectionPool_->acquire();

    auto abandonFn = [this, conn] noexcept {
      char *errMsg = nullptr;
      sqlite3_exec(conn->dbConnection, "ROLLBACK;", nullptr, nullptr, &errMsg);
      if (errMsg) {
        SPDLOG_CRITICAL("Failed to rollback transaction with abadonFn: {}", errMsg);
        sqlite3_free(errMsg);
      }
      connectionPool_->release(conn);
    };

    auto t = std::make_unique<Sqlite3Transaction>(conn, threadPool_, abandonFn);

    co_return std::move(t);
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

    connectionPool_->release(t->getConnection());
  }

private:
  std::vector<std::unique_ptr<Connection>> connections_;
  std::unique_ptr<core::AsyncPool<Connection>> connectionPool_;
  pool::ThreadPool *threadPool_;

  struct ConnectionReleaseGuard {
    Connection *c;
    core::AsyncPool<Connection> *ap;
    ~ConnectionReleaseGuard() { ap->release(c); }
  };
};

} // namespace rukh::db
