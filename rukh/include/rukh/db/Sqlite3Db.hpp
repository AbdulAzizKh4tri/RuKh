#pragma once

#include <cstdint>
#include <memory>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <string>
#include <unordered_map>

#include "IDatabase.hpp"
#include "rukh/Exceptions.hpp"

namespace rukh::db {

const int SQLITE3_BUSY_TIMEOUT = 5000; // ms to wait on SQLITE_BUSY instead of failing immediately

class Sqlite3Db : public IDatabase {
public:
  Sqlite3Db(const std::string &filename, int poolSize = 4) {
    for (int i = 0; i < poolSize; i++) {
      sqlite3 *db;
      int rc = sqlite3_open_v2(filename.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
      if (rc) {
        if (db)
          sqlite3_close(db);
        throw DatabaseException("Can't open database");
      }

      sqlite3_busy_timeout(db, SQLITE3_BUSY_TIMEOUT);

      auto conn = new Connection();
      conn->db = db;
      connectionQueue_.addConnection(conn);
    }

    Connection *conn = connectionQueue_.acquire();
    sqlite3_exec(conn->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    connectionQueue_.release(conn);
  }

  std::expected<QueryResult, DatabaseError> executeQuery(const std::string &sql,
                                                         const std::vector<DbValue> params) override {
    Connection *conn = connectionQueue_.acquire();

    ConnectionReleaseGuard connectionGuard{conn, &connectionQueue_};
    sqlite3 *db = conn->db;
    std::unordered_map<std::string, sqlite3_stmt *> &statements = conn->statements;

    sqlite3_stmt *stmt;
    if (statements.contains(sql)) {
      stmt = statements[sql];
    } else {
      int rc = sqlite3_prepare_v3(db, sql.c_str(), -1, SQLITE_PREPARE_PERSISTENT, &stmt, nullptr);
      if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db);
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
        throw DatabaseException("SQL error: " + std::string(sqlite3_errmsg(db)));
      }
    }

    result.affectedRows = sqlite3_changes(db);
    return result;
  }

private:
  struct StatementResetGuard {
    sqlite3_stmt *&s;
    ~StatementResetGuard() {
      sqlite3_reset(s);
      sqlite3_clear_bindings(s);
    }
  };

  struct Connection {
    sqlite3 *db;
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
        sqlite3_close(conn->db);
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

  ConnectionQueue connectionQueue_;

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
