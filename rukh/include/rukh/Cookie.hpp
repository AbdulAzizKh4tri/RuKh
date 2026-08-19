/**
 * @file Cookie.hpp
 * @brief Sweet cookies
 */

#pragma once
#include <chrono>
#include <string>

namespace rukh {

/// Sweet and delicious cookies
struct Cookie {
  std::string name;
  std::string value;
  std::string path = "/";
  bool httpOnly = false;
  bool secure = false;
  std::string sameSite = "Lax";
  int maxAge = -1;
  std::chrono::system_clock::time_point expires = std::chrono::system_clock::time_point::max();
  std::string domain = "";

  Cookie() {}

  Cookie(std::string name, std::string value) : name(std::move(name)), value(std::move(value)) {}

  Cookie(std::string name, std::string value, std::string path, int maxAge,
         std::chrono::system_clock::time_point expires)
      : name(std::move(name)), value(std::move(value)), path(std::move(path)), maxAge(maxAge), expires(expires) {}
};
} // namespace rukh
