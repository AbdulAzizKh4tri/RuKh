/**
 * @file ScopedTransaction.hpp
 * @brief Semi RAII wrapper for @ref rukh::db::ITransaction
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
 * @brief Semi RAII wrapper for @ref rukh::db::ITransaction
 *
 * @ref rukh::db::ITransaction::abandon "abandon"s transaction and warns user if ScopedTransaction goes out of scope
 * without commit/rollback.
 *
 * Construction is async (acquiring the underlying transaction may need to wait on the
 * connection pool). use @ref create instead of the constructor directly.
 *
 * @note can't rollback "cleanly" because destructors don't allow co_await. Open to suggestions if clean rollback is
 * possible.
 */
class ScopedTransaction : public ITransaction {
public:
  /**
   * @brief Acquire a transaction and wrap it. This is the only way to construct one,
   * acquiring may need to suspend, which a constructor can't do.
   */
  static core::Task<std::expected<ScopedTransaction, DatabaseError>> create(IDatabase *db) {
    auto t = co_await db->acquireTransaction();
    if (not t)
      co_return std::unexpected(t.error());
    co_return std::move(ScopedTransaction(db, std::move(*t)));
  }

  ScopedTransaction(ScopedTransaction &&other) noexcept : db_(other.db_), transaction_(std::move(other.transaction_)) {
    other.transaction_ = nullptr; // moved-from destructortor becomes a no-op below
  }

  ScopedTransaction &operator=(ScopedTransaction &&other) noexcept {
    if (this != &other) {
      db_ = other.db_;
      transaction_ = std::move(other.transaction_);
      other.transaction_ = nullptr;
    }
    return *this;
  }

  ScopedTransaction(const ScopedTransaction &) = delete;
  ScopedTransaction &operator=(const ScopedTransaction &) = delete;

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
   * @brief Release transaction if ended. Warn user and try the DB provided @ref rukh::db::ITransaction::abandon
   * "abandon" function if not.
   */
  ~ScopedTransaction() {
    if (!transaction_)
      return; // moved-from, nothing to release
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
  ScopedTransaction(IDatabase *db, std::unique_ptr<ITransaction> t) : db_(db), transaction_(std::move(t)) {}

  IDatabase *db_;
  std::unique_ptr<ITransaction> transaction_;
};
} // namespace rukh::db
