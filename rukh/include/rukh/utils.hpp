/**
 * @file utils.hpp
 * @brief Miscellaneous utility	functions
 */

#pragma once

#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace rukh {

// TODO: understand this
struct StringHash {
  using is_transparent = void;
  size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
  size_t operator()(const std::string &s) const { return std::hash<std::string_view>{}(s); }
};

inline std::chrono::steady_clock::time_point now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
  return std::chrono::steady_clock::time_point(std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec));
}

inline int digit_count(int x) {
  unsigned int v = (x < 0) ? -static_cast<unsigned int>(x) : static_cast<unsigned int>(x);

  if (v >= 1000000000u)
    return 10;
  if (v >= 100000000u)
    return 9;
  if (v >= 10000000u)
    return 8;
  if (v >= 1000000u)
    return 7;
  if (v >= 100000u)
    return 6;
  if (v >= 10000u)
    return 5;
  if (v >= 1000u)
    return 4;
  if (v >= 100u)
    return 3;
  if (v >= 10u)
    return 2;
  return 1;
}

/// Get a value from a map. If not found, return a default value
template <typename Map>
inline auto getOrDefault(const Map &map, const typename Map::key_type &key, typename Map::mapped_type defaultValue) ->
    typename Map::mapped_type {

  if (const auto it = map.find(key); it != map.end()) {
    return it->second;
  }
  return defaultValue;
}

/// Get the last value from a vector pair 'map' with the given key
inline auto getLastOrDefault(const std::vector<std::pair<std::string, std::string>> &mp, const std::string &key,
                             std::string defaultValue) -> std::string {

  auto it = std::find_if(mp.rbegin(), mp.rend(), [&key](const auto &p) { return p.first == key; });
  if (it != mp.rend())
    return it->second;
  return "";
}

/// Get all values from a vector pair 'map' with the given key
inline auto getAllValues(const std::vector<std::pair<std::string, std::string>> &mp, const std::string &key)
    -> std::vector<std::string> {
  std::vector<std::string> values;
  for (auto &[k, v] : mp) {
    if (k == key)
      values.push_back(v);
  }
  return values;
}

/// Convert a vector of pairs to a json object
inline auto toJsonObject(const std::vector<std::pair<std::string, std::string>> &pairs) {
  nlohmann::json j = nlohmann::json::object();
  for (const auto &[k, v] : pairs)
    j[k] = v;
  return j;
}

} // namespace rukh
