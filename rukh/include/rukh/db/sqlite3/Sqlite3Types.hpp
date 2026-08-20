/**
 * @file Sqlite3Types.hpp
 * @brief Types used for Sqlite3 implementation
 */
#pragma once

#include <condition_variable>
#include <sqlite3.h>
#include <unordered_map>
#include <vector>

/// RAII sqlite3_stmt guard
struct StatementResetGuard {
  sqlite3_stmt *&s;
  ~StatementResetGuard() {
    sqlite3_reset(s);
    sqlite3_clear_bindings(s);
  }
};

/// A Connection object with cached sqlite3_stmt statements.
struct Connection {
  sqlite3 *dbConnection;
  std::unordered_map<std::string, sqlite3_stmt *> statements;
};

/// A pool of sqlite3 connections, ensures a single connection isn't accessed by multiple threads.
struct ConnectionQueue {
public:
  /// Acquire a Connection, user *effectively* owns the connection and must release it.
  Connection *acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !available_.empty(); });
    auto conn = available_.back();
    available_.pop_back();
    return conn;
  }

  /// Release the acquire()'d Connection back to the pool.
  void release(Connection *conn) {
    std::unique_lock<std::mutex> lock(mutex_);
    available_.push_back(conn);
    cv_.notify_one();
  }

  /// Add a Conneciton to the pool
  void addConnection(Connection *conn) {
    available_.push_back(conn);
    allConnections_.push_back(conn);
  }

  /// Close all db connections after `sqlite3_finalize()`ing their statements
  ~ConnectionQueue() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto conn : allConnections_) {
      for (auto &[_, statement] : conn->statements)
        sqlite3_finalize(statement);
      sqlite3_close(conn->dbConnection);
      delete conn;
    }
  }

private:
  std::vector<Connection *> available_;
  std::vector<Connection *> allConnections_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

/// RAII Connection scope guard
struct ConnectionReleaseGuard {
  Connection *c;
  ConnectionQueue *q;
  ~ConnectionReleaseGuard() { q->release(c); }
};
