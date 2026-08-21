/**
 * @file stringUtils.hpp
 * @brief String utility functions
 */
#pragma once

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

namespace rukh {

/// Return a comma seperated string from a vector of strings
inline std::string getCommaSeparatedString(const std::vector<std::string> &strings) {

  if (strings.empty())
    return {};

  size_t total = 2 * (strings.size() - 1); // ", " separators
  for (auto &s : strings)
    total += s.size();

  std::string result;
  result.reserve(total);

  for (size_t i = 0; i < strings.size(); i++) {
    if (i > 0)
      result += ", ";
    result += strings[i];
  }
  return result;
}

/// Case insensitive contains
inline bool icontains(std::string_view haystack, std::string_view needle) {
  if (needle.empty())
    return true;
  if (needle.size() > haystack.size())
    return false;

  auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                        [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
  return it != haystack.end();
}

/// Convert string_view to lower case string
inline std::string toLowerCase(std::string_view s) {
  char buf[64];
  if (s.size() < sizeof(buf)) {
    std::transform(s.begin(), s.end(), buf, [](unsigned char c) { return std::tolower(c); });
    return std::string(buf, s.size());
  }
  std::string result(s);
  std::ranges::transform(result, result.begin(), [](unsigned char c) { return std::tolower(c); });
  return result;
}

/// Split a string into a vector of strings based on a delimiter
inline std::vector<std::string> split(std::string_view s, std::string_view delim = " ") {
  auto parts =
      s | std::views::split(delim) | std::views::transform([](auto &&r) { return std::string(r.begin(), r.end()); });

  return {parts.begin(), parts.end()};
}

/// Remove whitespace from the start and end of a string.
inline void trim(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));

  s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
}

/// Remove whitespace from the start and end of a string_view.
inline void trim(std::string_view &s) {
  while (!s.empty() && std::isspace((unsigned char)s.front()))
    s.remove_prefix(1);
  while (!s.empty() && std::isspace((unsigned char)s.back()))
    s.remove_suffix(1);
}

} // namespace rukh
