#include <rukh/http/middleware/SessionMiddleware.hpp>

#include <memory>

#include <rukh/TypeHelpers.hpp>
#include <rukh/http/Cookie.hpp>
#include <rukh/http/session/SessionHandle.hpp>

namespace rukh::http::middleware {

SessionMiddleware::SessionMiddleware(SessionConfig SessionConfig, std::unique_ptr<ISessionStore> sessionStore)
    : sessionConfig_(SessionConfig), sessionStore_(std::move(sessionStore)) {}

core::Task<Response> SessionMiddleware::operator()(HttpRequest &request, Next next) {
  auto sessionCookieOpt = request.getCookie("session_id");
  std::string sessionCookie;

  if (sessionCookieOpt.has_value()) {
    sessionCookie = *sessionCookieOpt;
  }

  SessionHandle sessionHandle(sessionStore_.get(), sanitize(sessionCookie));
  request.setSessionHandle(&sessionHandle);

  Response response = co_await next();

  if (not sessionHandle.wasLoaded())
    co_return response;

  Session &session = *sessionHandle.session;

  if (session.isInvalidated()) {
    if (not sessionHandle.id.empty())
      co_await sessionStore_->destroy(sessionHandle.id);

    std::visit(overloads{[](auto &res) { res.cookies.deleteCookie("session_id"); }}, response);
    co_return response;
  }

  if (not session.isDirty())
    co_return response;

  if (sessionHandle.id.empty())
    sessionHandle.id = sessionStore_->generateId();

  co_await sessionStore_->save(sessionHandle.id, session);
  std::visit(overloads{[this, &sessionHandle](auto &res) {
               Cookie sessionCookie("session_id", sessionHandle.id);
               sessionCookie.secure = sessionConfig_.cookieSecure;
               sessionCookie.httpOnly = sessionConfig_.cookieHttpOnly;
               res.cookies.setCookie(sessionCookie);
             }},
             response);

  co_return response;
}

std::string SessionMiddleware::sanitize(std::string id) const {
  if (id.size() < sessionConfig_.minIdSize || id.size() > sessionConfig_.maxIdSize)
    return "";
  for (unsigned char c : id)
    if (!std::isalnum(c) && c != '-' && c != '_')
      return "";
  return id;
}
} // namespace rukh::http::middleware
