#pragma once

#include <expected>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

#include <rukh/core/BodyStream.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/middleware/Session.hpp>

namespace rukh {

struct SessionHandle;

enum class ContentLengthError {
  NO_CONTENT_LENGTH_HEADER,
  INVALID_CONTENT_LENGTH,
};

class HttpRequest {
public:
  using Range = std::pair<std::optional<size_t>, std::optional<size_t>>;

  static constexpr std::array singletonHeaders_ = {
      std::string_view("host"),          std::string_view("content-length"),
      std::string_view("content-type"),  std::string_view("transfer-encoding"),
      std::string_view("authorization"), std::string_view("expect"),
  };

  HttpRequest();

  bool parseRequestHeader(std::string_view headerView);

  core::Task<nlohmann::json> jsonBody();

  core::Task<std::unordered_map<std::string, std::vector<std::string>>> getFormData();

  std::vector<Range> getRanges() const;

  std::vector<std::pair<std::string, std::string>> getCookies() const;

  std::optional<std::string> getCookie(const std::string &name) const;

  core::Task<Session *> getSession();

  std::expected<size_t, ContentLengthError> getContentLength();

  std::string_view getContentType() const;

  std::string_view getHeader(const std::string &name) const;

  std::string_view getHeaderLower(const std::string &lowerKey) const;

  std::vector<std::string> getHeaders(const std::string &name) const;

  std::vector<std::pair<std::string, std::string>> getAllHeaders() const;

  void setHeader(const std::string &name, const std::string &value);

  // Always use the Lower version of these whenever possible, avoids a string allocation.
  // But be careful to only pass lowercased keys
  void setHeaderLower(const std::string_view &lowercaseKey, const std::string &value);

  void addHeader(const std::string &name, const std::string &value);
  void addHeaderLower(const std::string_view &lowercaseKey, const std::string &value);
  void addHeaderLower(const std::string_view &lowercaseKey, const std::string_view &value);

  void removeHeader(const std::string &name);

  void setAttribute(const std::string &key, const std::string &value);

  std::string getAttribute(const std::string &key, std::string defaultValue = "") const;

  std::string getQueryParam(const std::string &key, std::string defaultValue = "") const;

  std::vector<std::string> getQueryParams(const std::string &key) const;

  std::vector<std::pair<std::string, std::string>> getAllQueryParams() const;

  std::string getPathParam(const std::string &key, std::string defaultValue = "") const;

  void setPathParams(const std::vector<std::pair<std::string, std::string>> &pathParams);

  std::vector<std::pair<std::string, std::string>> getAllPathParams() const;

  core::Task<std::string> consumeBody();
  BodyStream *bodyStream();
  void attachBodyStream(std::unique_ptr<BodyStream> bodyStream);

  const std::string &getPath() const;
  const std::string &getRawPath() const;
  const std::string &getVersion() const;
  const std::vector<std::string_view> &getPathParts() const;

  const std::string &getMethod() const;
  void setMethod(const std::string &method);

  const std::string &getIp() const;
  uint16_t getPort() const;

  void setIp(const std::string &ip);
  void setPort(uint16_t port);
  void setSessionHandle(SessionHandle *sessionHandle);

  void reset(const std::string &ip, uint16_t port);

private:
  std::vector<std::pair<std::string, std::string>> headers_;
  std::vector<std::pair<std::string, std::string>> queryParams_;
  std::vector<std::pair<std::string, std::string>> attributes_;
  std::vector<std::pair<std::string, std::string>> pathParams_;

  std::optional<size_t> contentLength_;
  std::unique_ptr<BodyStream> bodyStream_;

  SessionHandle *sessionHandle_ = nullptr;

  std::string method_, rawPath_, path_, version_, ip_;
  uint16_t port_ = 0;
  std::vector<std::string_view> pathParts_;

  bool parseRequestLine(std::string_view requestLine);

  bool parseRequestHeaders(std::string_view headerView);

  void parsePathAndQueryParams(std::string_view rawPathView);
};
} // namespace rukh
