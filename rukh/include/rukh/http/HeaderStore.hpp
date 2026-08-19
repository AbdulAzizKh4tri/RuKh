/**
 * @file HeaderStore.hpp
 * @brief Common header store for both Http Response types
 */

#pragma once

#include <vector>

#include <rukh/stringUtils.hpp>

namespace rukh::http {

/// Common header store for both Http Response types
class HeaderStore {
public:
  HeaderStore() { headers_.reserve(4); }

  /**
   * @brief get header with the given name. If multiple, returns the last one. Prefer @ref getHeaderLower.
   */
  std::string getHeader(const std::string &name) const { return getLastOrDefault(headers_, toLowerCase(name), ""); }

  /**
   * @brief get header with the given name. If multiple, returns the last one
   *
   * @warning Lower means the provided KEY is lowercase, the header will be returned as is. All headers are stored with
   * a lowercase key
   */
  std::string getHeaderLower(const std::string &name) const { return getLastOrDefault(headers_, name, ""); }

  /// All headers with the given name. Prefer @ref getHeadersLower
  std::vector<std::string> getHeaders(const std::string &name) const {
    return getAllValues(headers_, toLowerCase(name));
  }

  /**
   * @brief All headers with the given name.
   * @warning Lower means the provided KEY is lowercase. All headers are stored with a lowercase key.
   */
  std::vector<std::string> getHeadersLower(const std::string &name) const { return getAllValues(headers_, name); }

  std::vector<std::pair<std::string, std::string>> &getAllHeaders() { return headers_; }
  const std::vector<std::pair<std::string, std::string>> &getAllHeaders() const { return headers_; }

  /// Set the header with the given name. Override any existing. Prefer @ref setHeaderLower
  void setHeader(const std::string &name, const std::string &value) {
    std::string lowerKey = toLowerCase(name);
    std::erase_if(headers_, [&lowerKey](const auto &p) { return p.first == lowerKey; });
    headers_.emplace_back(lowerKey, value);
  }

  /**
   * @warning Lower means the provided KEY is lowercase. All headers are stored with a lowercase key.
   */
  void setHeaderLower(const std::string_view &lowercaseKey, const std::string_view value) {
    std::erase_if(headers_, [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; });
    headers_.emplace_back(lowercaseKey, value);
  }

  /// Add header with given name. Ignores existing ones. Prefer @ref addHeaderLower
  void addHeader(const std::string &name, const std::string &value) {
    auto key = toLowerCase(name);
    if (std::ranges::contains(singletonHeaders_, key)) {
      if (std::find_if(headers_.begin(), headers_.end(), [&key](const auto &p) { return p.first == key; }) ==
          headers_.end())
        headers_.emplace_back(key, value);
      return;
    }
    headers_.emplace_back(key, value);
  }

  /**
   * @warning Lower means the provided KEY is lowercase. All headers are stored with a lowercase key.
   */
  void addHeaderLower(const std::string_view &lowercaseKey, const std::string &value) {
    if (std::ranges::contains(singletonHeaders_, lowercaseKey)) {
      if (std::find_if(headers_.begin(), headers_.end(),
                       [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; }) == headers_.end())
        headers_.emplace_back(lowercaseKey, value);
      return;
    }
    headers_.emplace_back(lowercaseKey, value);
  }

  std::vector<std::pair<std::string, std::string>> &getHeaders() { return headers_; }

  /// Remove all headers with the given name.
  void removeHeader(const std::string &name) {
    auto key = toLowerCase(name);
    std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
  }

  /**
   * @name Helpers for common Headers.
   * @{
   */
  void setCacheControl(const std::string &cacheControlHeader) { setHeaderLower("cache-control", cacheControlHeader); }
  void setContentRange(size_t start, size_t end, size_t filesize) {
    setHeaderLower("content-range",
                   "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(filesize));
  }
  /** @} */

private:
  std::vector<std::pair<std::string, std::string>> headers_;

  static constexpr std::array singletonHeaders_ = {
      std::string_view("content-length"), std::string_view("content-type"), std::string_view("transfer-encoding"),
      std::string_view("date"),           std::string_view("server"),       std::string_view("location"),
  };
};
} // namespace rukh::http
