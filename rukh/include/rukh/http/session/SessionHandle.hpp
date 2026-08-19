#pragma once

#include <rukh/http/session/ISessionStore.hpp>

namespace rukh::http  {

struct SessionHandle {
  ISessionStore *sessionStore;
  std::string id;
  std::optional<Session> session;

  SessionHandle(ISessionStore *store, std::string id) : sessionStore(store), id(std::move(id)) {}

  core::Task<Session *> get() {
    if (session.has_value())
      co_return &*session;

    if (not id.empty()) {
      auto sessionOpt = co_await sessionStore->load(id);
      if (sessionOpt.has_value()) {
        session = std::move(*sessionOpt);
        session->markLoaded();
        co_return &*session;
      }
    }

    session.emplace();
    co_return &*session;
  };

  bool wasLoaded() const { return session.has_value(); }
};
} // namespace rukh
