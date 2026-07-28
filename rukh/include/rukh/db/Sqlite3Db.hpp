#pragma once

#include <expected>
#include <memory>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <string>

#include <rukh/Exceptions.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/db/Sqlite3QueryExecutor.hpp>
#include <rukh/db/Sqlite3Transaction.hpp>
#include <rukh/db/Sqlite3Types.hpp>

namespace rukh::db {
// TODO: handle exec() errors

const int SQLITE3_BUSY_TIMEOUT = 5000; // ms to wait on SQLITE_BUSY instead of failing immediately

class Sqlite3Db : public IDatabase {
public:
  Sqlite3Db(const std::string &filename, int poolSize = 4) {
    for (int i = 0; i < poolSize; i++) {
      sqlite3 *dbConnection;
      int rc = sqlite3_open_v2(filename.c_str(), &dbConnection, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
      if (rc) {
        if (dbConnection)
          sqlite3_close(dbConnection);
        throw DatabaseException("Can't open database");
      }

      sqlite3_busy_timeout(dbConnection, SQLITE3_BUSY_TIMEOUT);

      auto conn = new Connection();
      conn->dbConnection = dbConnection;
      connectionQueue_.addConnection(conn);
    }

    Connection *conn = acquireConnection();
    sqlite3_exec(conn->dbConnection, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    releaseConnection(conn);
  }

  std::expected<QueryResult, DatabaseError> executeQuery(const std::string &sql,
                                                         const std::vector<DbValue> &params = {}) override {
    Connection *conn = acquireConnection();
    ConnectionReleaseGuard connectionGuard{conn, &connectionQueue_};

    return Sqlite3QueryExecutor::executeOnConnection(conn, sql, params);
  }

  std::expected<std::unique_ptr<ITransaction>, DatabaseError> startTransaction() override {
    Connection *conn = acquireConnection();
    auto t = Sqlite3Transaction::createTransaction(conn);
    if (not t) {
      releaseConnection(conn);
      return std::unexpected(t.error());
    }
    return std::move(*t);
  };

  void endTransaction(std::unique_ptr<ITransaction> transaction) override {
    if (transaction == nullptr) {
      SPDLOG_WARN("Attempted to end null Transaction");
      return;
    }

    Sqlite3Transaction *t = dynamic_cast<Sqlite3Transaction *>(transaction.get());
    if (t == nullptr) {
      SPDLOG_ERROR("endTransaction() called with a Transaction not created by Sqlite3Db");
      return;
    }

    releaseConnection(t->getConnection());
    transaction.reset();
  }

  Connection *acquireConnection() { return connectionQueue_.acquire(); }
  void releaseConnection(Connection *conn) { connectionQueue_.release(conn); }

private:
  ConnectionQueue connectionQueue_;
};

} // namespace rukh::db
