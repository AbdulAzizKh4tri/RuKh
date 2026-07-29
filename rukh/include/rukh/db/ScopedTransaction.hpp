#pragma once

#include <memory>

#include <rukh/db/IDatabase.hpp>
#include <rukh/Exceptions.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/ITransaction.hpp>

namespace rukh::db {

class ScopedTransaction {
public:
  ScopedTransaction(IDatabase *db) : db_(db) {
    auto t = db_->startTransaction();
    if (not t)
      throw DatabaseException("Failed to start transaction " + t.error().message);
    transaction_ = std::move(*t);
  }

  ~ScopedTransaction() {
    if (not transaction_->isTransactionEnded())
      transaction_->rollback();
    db_->endTransaction(std::move(transaction_));
  }

  ITransaction *operator->() const noexcept { return transaction_.get(); }

  ITransaction *operator*() const noexcept { return transaction_.get(); }

private:
  IDatabase *db_;
  std::unique_ptr<ITransaction> transaction_;
};

} // namespace rukh::db
