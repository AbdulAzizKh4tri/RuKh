/**
 * @file InMemorySessionStore.hpp
 * @brief An in-memory implementation of ISessionStore
 */
#pragma once

#include <chrono>
#include <mutex>
#include <openssl/rand.h>
#include <optional>
#include <shared_mutex>

#include <rukh/core/Task.hpp>
#include <rukh/http/session/ISessionStore.hpp>

namespace rukh::http {

/// An in-memory implementation of ISessionStore
class InMemorySessionStore : public ISessionStore {
public:
  /// Session entry with last accessed time for ttl invalidation
  struct SessionEntry {
    Session session;
    std::chrono::time_point<std::chrono::system_clock> lastAccessed;
  };

  InMemorySessionStore(std::chrono::seconds ttl) : ttl_(ttl) {}

  /**
   * @brief Load session with the given @p id.
   * @returns Session if found, std::nullopt if not found or invalidated
   */
  core::Task<std::optional<Session>> load(const std::string &id) override {
    {
      std::shared_lock readLock(mutex_);
      auto entryIt = sessions_.find(id);
      if (entryIt == sessions_.end()) {
        co_return std::nullopt;
      }

      struct timespec ts;
      clock_gettime(CLOCK_REALTIME_COARSE, &ts);

      const auto &entry = entryIt->second;
      if (not(std::chrono::system_clock::from_time_t(ts.tv_sec) - entry.lastAccessed > ttl_ ||
              entry.session.isInvalidated())) {
        co_return entry.session;
      }
    }

    std::unique_lock writeLock(mutex_);
    if (auto it = sessions_.find(id); it != sessions_.end())
      sessions_.erase(it);
    co_return std::nullopt;
  };

  /// Save Session with @p id
  core::Task<void> save(const std::string &id, const Session &session) override {
    std::unique_lock writeLock(mutex_);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME_COARSE, &ts);
    sessions_[id] = {session, std::chrono::system_clock::from_time_t(ts.tv_sec)};
    co_return;
  };

  /// delete session
  core::Task<void> destroy(const std::string &id) override {
    std::unique_lock writeLock(mutex_);
    sessions_.erase(id);
    co_return;
  };

  /// generate random id
  std::string generateId() override {
    unsigned char buf[32];
    if (RAND_bytes(buf, sizeof(buf)) != 1)
      throw std::runtime_error("RAND_bytes failed");
    std::string id(64, '\0');
    static constexpr char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
      id[i * 2] = hex[buf[i] >> 4];
      id[i * 2 + 1] = hex[buf[i] & 0xf];
    }
    return id;
  }

private:
  std::shared_mutex mutex_;
  std::unordered_map<std::string, SessionEntry> sessions_;
  std::chrono::seconds ttl_;
};
} // namespace rukh::http
