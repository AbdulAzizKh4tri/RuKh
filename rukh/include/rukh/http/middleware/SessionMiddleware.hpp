/**
 * @file SessionMiddleware.hpp
 * @brief Session middleware
 */
#pragma once

#include <memory>

#include <rukh/core/Task.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpTypes.hpp>
#include <rukh/http/session/ISessionStore.hpp>

namespace rukh::http::middleware {

/// config for session, used by SessionMiddleware
struct SessionConfig {
  size_t minIdSize = 8;
  size_t maxIdSize = 64;
  bool cookieHttpOnly = false;
  bool cookieSecure = false;
};

/**
 * @brief Session middleware. Checks for session_id and attaches a SessionHandle to the request.
 *
 * After the request is processed:\n
 * If the session was invalidated, deletes the session from store and deletes the session cookie.\n
 * If the session was modified/created, stores the session in the session store and sets the session cookie.
 */
class SessionMiddleware {
public:
  SessionMiddleware(SessionConfig SessionConfig, std::unique_ptr<ISessionStore> sessionStore);
  core::Task<Response> operator()(HttpRequest &request, Next next);

private:
  SessionConfig sessionConfig_;
  std::unique_ptr<ISessionStore> sessionStore_;

  std::string sanitize(std::string id) const;
};
} // namespace rukh::http::middleware
