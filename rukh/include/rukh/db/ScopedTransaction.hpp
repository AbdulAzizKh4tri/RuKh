#pragma once

#include <memory>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>

namespace rukh::db {

class ScopedTransaction : public ITransaction {
public:
  ScopedTransaction(IDatabase *db) : db_(db) {
    auto t = db_->acquireTransaction();
    if (not t)
      throw DatabaseException("Failed to start transaction " + t.error().message);
    transaction_ = std::move(*t);
  }

  Task<std::expected<QueryResult, DatabaseError>> begin(const std::string &mode = "DEFERRED") override {
    co_return co_await transaction_->begin(mode);
  }
  Task<std::expected<QueryResult, DatabaseError>> commit() override { co_return co_await transaction_->commit(); };

  Task<std::expected<QueryResult, DatabaseError>> rollback() override { co_return co_await transaction_->rollback(); };

  Task<std::expected<QueryResult, DatabaseError>> executeQuery(const std::string &query,
                                                               const std::vector<DbValue> params = {}) override {
    co_return co_await transaction_->executeQuery(query, params);
  }

  void abandon() override {
    transaction_->abandon();
  }

  bool isTransactionEnded() const override { return transaction_->isTransactionEnded(); }

  ~ScopedTransaction() {
    if (isTransactionEnded()) {
      db_->releaseTransaction(transaction_.get());
      transaction_.reset();
      return;
    }
    SPDLOG_WARN("ScopedTransaction went out of scope without ending transaction, must co_await commit() or "
                "rollback(). Attempting to run cleanup function.");
    abandon();
    transaction_.reset();
  }

private:
  IDatabase *db_;
  std::unique_ptr<ITransaction> transaction_;
};

} // namespace rukh::db
