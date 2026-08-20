/**
 * @file ScopedTransaction.hpp
 * @brief Semi RAII wrapper for @ref ITransaction
 */
#pragma once

#include <memory>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>

namespace rukh::db {

/**
 * @brief Semi RAII wrapper for @ref ITransaction
 *
 * @ref ITransaction::abandon "abandon"s transaction and warns user if ScopedTransaction goes out of scope
 * without commit/rollback.
 *
 * @note can't rollback "cleanly" because destructors don't allow co_await. Open to suggestions if clean rollback is
 * possible.
 */
class ScopedTransaction : public ITransaction {
public:
  /// Acquire a transaction, ownership transfers to `this` object.
  ScopedTransaction(IDatabase *db) : db_(db) {
    auto t = db_->acquireTransaction();
    if (not t)
      throw DatabaseException("Failed to start transaction " + t.error().message);
    transaction_ = std::move(*t);
  }

  /// @name Forwarders
  /// Forwards to whatever ITransaction the DB provides.@{
  core::Task<std::expected<QueryResult, DatabaseError>> begin(const std::string &mode = "DEFERRED") override {
    return transaction_->begin(mode);
  }
  core::Task<std::expected<QueryResult, DatabaseError>> commit() override { return transaction_->commit(); };
  core::Task<std::expected<QueryResult, DatabaseError>> rollback() override { return transaction_->rollback(); };
  core::Task<std::expected<QueryResult, DatabaseError>> executeQuery(const std::string &query,
                                                                     const std::vector<DbValue> params = {}) override {
    return transaction_->executeQuery(query, params);
  }
  void abandon() override { transaction_->abandon(); }
  bool isTransactionEnded() const override { return transaction_->isTransactionEnded(); }
  /// @}

  /**
   * @brief Release transaction if ended. Warn user and try the DB provided @ref ITransaction::abandon "abandon"
   * function if not.
   */
  ~ScopedTransaction() {
    if (isTransactionEnded()) {
      db_->releaseTransaction(transaction_.get());
      transaction_.reset();
      return;
    }
    SPDLOG_ERROR("ScopedTransaction went out of scope without ending transaction, must co_await commit() or "
                 "rollback(). Attempting to run abandon function.");
    abandon();
    transaction_.reset();
  }

private:
  IDatabase *db_;
  std::unique_ptr<ITransaction> transaction_;
};

} // namespace rukh::db
