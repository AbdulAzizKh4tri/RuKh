/**
 * @file CookieStore.hpp
 * @brief Common cookie store for both Http Response types
 */

#pragma once

#include <chrono>
#include <vector>

#include <rukh/Cookie.hpp>
#include <rukh/core/utils.hpp>

namespace rukh {

/// Common cookie store for both Http Response types
class CookieStore {
public:
  /// Adds cookie to the response
  void setCookie(const Cookie &cookie) { cookies_.push_back(cookie); }

  /// Removes all cookies with the given @p name from this response
  void unsetCookie(const std::string &name) {
    std::erase_if(cookies_, [&name](const auto &p) { return p.name == name; });
  }

  /// Attach an expiration cookie with the given @p name
  void deleteCookie(const std::string &name, const std::string &path = "/") {
    cookies_.emplace_back(name, "", path, 0, std::chrono::system_clock::from_time_t(0));
  }

  std::vector<Cookie> getAllCookies() const { return cookies_; }

  std::optional<Cookie> getCookie(const std::string &name) const {
    for (const auto &cookie : cookies_)
      if (cookie.name == name)
        return cookie;
    return std::nullopt;
  }

  /// @cond INTERNAL

  template <typename WriteFn> void serializeUsing(WriteFn &&write) const {
    for (const auto &cookie : cookies_) {
      write("set-cookie: ");
      write(cookie.name);
      write("=");
      write(cookie.value);
      write("; Path=");
      write(cookie.path);
      write("; SameSite=");
      write(cookie.sameSite);

      if (cookie.httpOnly)
        write("; HttpOnly");
      if (cookie.secure)
        write("; Secure");

      if (cookie.domain != "") {
        write("; Domain=");
        write(cookie.domain);
      }

      if (cookie.expires != std::chrono::system_clock::time_point::max()) {
        write("; Expires=");
        write(toHttpDate(cookie.expires));
      }
      if (cookie.maxAge > -1) {
        write("; Max-Age=");
        char tmp[20];
        auto [ptr, ec] = std::to_chars(tmp, tmp + 20, cookie.maxAge);
        write({tmp, static_cast<std::size_t>(ptr - tmp)});
      }
      write("\r\n");
    }
  }

  size_t getSerializedSize() const {
    size_t total = 0;
    for (const auto &cookie : cookies_) {
      total += (sizeof("Set-Cookie: ") - 1);

      total += cookie.name.size() + (sizeof("=") - 1) + cookie.value.size() + (sizeof("; Path=") - 1) +
               cookie.path.size() + (sizeof("; SameSite=") - 1) + cookie.sameSite.size();

      if (cookie.httpOnly)
        total += (sizeof("; HttpOnly") - 1);
      if (cookie.secure)
        total += (sizeof("; Secure") - 1);

      if (cookie.domain != "")
        total += (sizeof("; Domain=") - 1) + cookie.domain.size();

      if (cookie.expires != std::chrono::system_clock::time_point::max())
        total += (sizeof("; Expires=") - 1) + toHttpDate(cookie.expires).size();
      if (cookie.maxAge > -1) {
        total += (sizeof("; Max-Age=") - 1) + digit_count(cookie.maxAge);
      }

      total += (sizeof("\r\n") - 1);
    }
    return total;
  }

  /// @endcond

private:
  std::vector<Cookie> cookies_;
};
} // namespace rukh
