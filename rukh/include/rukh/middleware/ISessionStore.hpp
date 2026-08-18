#pragma once

#include <rukh/core/Task.hpp>
#include <rukh/middleware/Session.hpp>

namespace rukh {

class ISessionStore {
public:
  virtual core::Task<std::optional<Session>> load(const std::string &id) = 0;
  virtual core::Task<void> save(const std::string &id, const Session &session) = 0;
  virtual core::Task<void> destroy(const std::string &id) = 0;
  virtual std::string generateId() = 0;
  virtual ~ISessionStore() = default;
};
} // namespace rukh
