#pragma once

#include <memory>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpTypes.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/middleware/ISessionStore.hpp>

namespace rukh {

struct SessionConfig {
  size_t minIdSize = 8;
  size_t maxIdSize = 64;
  bool cookieHttpOnly = false;
  bool cookieSecure = false;
};

class SessionMiddleware {
public:
  SessionMiddleware(SessionConfig SessionConfig, std::unique_ptr<ISessionStore> sessionStore);
  core::Task<Response> operator()(HttpRequest &request, Next next);

private:
  SessionConfig sessionConfig_;
  std::unique_ptr<ISessionStore> sessionStore_;

  std::string sanitize(std::string id) const;
};
} // namespace rukh
