#pragma once

#include <condition_variable>
#include <sqlite3.h>
#include <unordered_map>
#include <vector>

struct StatementResetGuard {
  sqlite3_stmt *&s;
  ~StatementResetGuard() {
    sqlite3_reset(s);
    sqlite3_clear_bindings(s);
  }
};

struct Connection {
  sqlite3 *dbConnection;
  std::unordered_map<std::string, sqlite3_stmt *> statements;
};

struct ConnectionQueue {
public:
  Connection *acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !available_.empty(); });
    auto conn = available_.back();
    available_.pop_back();
    return conn;
  }

  void release(Connection *conn) {
    std::unique_lock<std::mutex> lock(mutex_);
    available_.push_back(conn);
    cv_.notify_one();
  }

  void addConnection(Connection *conn) { available_.push_back(conn); }

  ~ConnectionQueue() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto conn : available_) {
      for (auto &[_, statement] : conn->statements)
        sqlite3_finalize(statement);
      sqlite3_close(conn->dbConnection);
      delete conn;
    }
  }

private:
  std::vector<Connection *> available_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

struct ConnectionReleaseGuard {
  Connection *c;
  ConnectionQueue *q;
  ~ConnectionReleaseGuard() { q->release(c); }
};
