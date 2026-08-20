/**
 * @file ISessionStore.hpp
 * @brief Interface for session store
 */

#pragma once

#include <rukh/core/Task.hpp>
#include <rukh/http/session/Session.hpp>

namespace rukh::http {

/// Interface for session store
class ISessionStore {
public:
  /// Load session from session store (memory/redis/db/etc)
  virtual core::Task<std::optional<Session>> load(const std::string &id) = 0;

  /// Save session to session store with @p id.
  virtual core::Task<void> save(const std::string &id, const Session &session) = 0;

  /// Delete session from session store
  virtual core::Task<void> destroy(const std::string &id) = 0;

  /// Generate id to be used as session identifier
  virtual std::string generateId() = 0;

  virtual ~ISessionStore() = default;
};
} // namespace rukh::http
