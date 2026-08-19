/**
 * @file httpUtils.hpp
 * @brief HTTP utility functions
 */

#pragma once

#include <rukh/stringUtils.hpp>

namespace rukh::http {

static constexpr std::array<unsigned char, 4> crlf2 = {'\r', '\n', '\r', '\n'};
static constexpr std::array<unsigned char, 2> crlf = {'\r', '\n'};

inline void normalizePath(std::string &path) {

  if (path.find("//") == std::string::npos) {
    if (!path.empty() && path.front() == '/')
      path.erase(0, 1);
    if (!path.empty() && path.back() == '/')
      path.pop_back();
    return;
  }

  auto newEnd = std::unique(path.begin(), path.end(), [](char a, char b) { return a == '/' && b == '/'; });
  path.erase(newEnd, path.end());

  if (!path.empty() && path.front() == '/')
    path.erase(0, 1);

  if (!path.empty() && path.back() == '/')
    path.pop_back();
}

inline std::string percentDecode(std::string_view s) {
  std::string result;
  result.reserve(s.size());

  auto fromHex = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };

  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      result += ' ';
    } else if (s[i] == '%' && i + 2 < s.size()) {
      int hi = fromHex(s[i + 1]);
      int lo = fromHex(s[i + 2]);
      if (hi != -1 && lo != -1) {
        result += static_cast<char>((hi << 4) | lo);
        i += 2;
      } else {
        result += s[i]; // malformed — keep as-is
      }
    } else {
      result += s[i];
    }
  }

  return result;
}

inline const std::string &getCurrentHttpDate() {
  thread_local static std::string cached;
  thread_local static time_t lastTime = 0;

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME_COARSE, &ts);

  if (ts.tv_sec != lastTime) {
    std::tm tm{};
    gmtime_r(&ts.tv_sec, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    cached = buf;
    lastTime = ts.tv_sec;
  }
  return cached;
}

inline std::string toHttpDate(std::chrono::system_clock::time_point tp) {
  time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
  return buf;
}

inline bool mime_match(std::string_view p, std::string_view v) {
  if (p == "*/*")
    return true;
  auto ps = p.find('/'), vs = v.find('/');
  if (ps == std::string_view::npos || vs == std::string_view::npos)
    return false;
  return (p[0] == '*' || v[0] == '*' || p.substr(0, ps) == v.substr(0, vs)) &&
         (p[ps + 1] == '*' || v[vs + 1] == '*' || p.substr(ps + 1) == v.substr(vs + 1));
}

// Wed, 01 Jan 2025 12:00:00 GMT
inline std::optional<std::chrono::system_clock::time_point> parseHttpDate(const std::string &date) {
  std::tm tm{};
  std::istringstream ss(date);
  ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
  if (ss.fail())
    return std::nullopt;
  return std::chrono::system_clock::from_time_t(timegm(&tm));
}

inline void parseQValues(const std::string_view acceptString, std::vector<std::pair<std::string, float>> &typePrefs,
                         std::vector<std::string> &excluded) {
  for (auto typeQ : split(acceptString, ",")) {
    trim(typeQ);
    auto sepIt = typeQ.find(';');
    std::string type = typeQ.substr(0, sepIt);
    trim(type);
    if (type.empty())
      continue;
    float q = 1.0f;
    if (sepIt != std::string::npos) {
      std::string params = typeQ.substr(sepIt + 1);
      auto qpos = params.find("q=");
      if (qpos != std::string::npos) {
        auto qval = params.substr(qpos + 2);
        trim(qval);
        q = std::clamp(std::strtof(qval.c_str(), nullptr), 0.0f, 1.0f);
      }
    }
    if (q == 0.0f) {
      excluded.push_back(type);
      continue;
    }
    typePrefs.emplace_back(type, q);
  }
}

} // namespace rukh::http
