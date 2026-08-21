/**
 * @file HttpRequest.hpp
 * @brief Rukh's HTTP Request object
 */
#pragma once

#include <expected>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

#include <rukh/core/Task.hpp>
#include <rukh/http/BodyStream.hpp>
#include <rukh/http/session/Session.hpp>

namespace rukh::http {

struct SessionHandle;

enum class ContentLengthError {
  NO_CONTENT_LENGTH_HEADER,
  INVALID_CONTENT_LENGTH,
};

/// Rukh's HTTP Request object
class HttpRequest {
public:
  /// used for range queries
  using Range = std::pair<std::optional<size_t>, std::optional<size_t>>;

  static constexpr std::array singletonHeaders_ = {
      std::string_view("host"),          std::string_view("content-length"),
      std::string_view("content-type"),  std::string_view("transfer-encoding"),
      std::string_view("authorization"), std::string_view("expect"),
  };

  HttpRequest();

  /// Parse the request headers, @p headerView does not include the trailing \\r\\n
  bool parseRequestHeader(std::string_view headerView);

  /**
   * @brief Consume the full request body stream and interpret it as a json object.
   *
   * @see consumeBody();
   * @see bodyStream;
   */
  core::Task<nlohmann::json> jsonBody();

  /**
   * @brief Consume the full request body stream and interpret it as url-encoded form-data.
   *
   * @see consumeBody();
   * @see bodyStream;
   */
  core::Task<std::unordered_map<std::string, std::vector<std::string>>> getFormData();

  /// Parse and get the Range header as a set of ranges
  std::vector<Range> getRanges() const;

  std::vector<std::pair<std::string, std::string>> getCookies() const;

  std::optional<std::string> getCookie(const std::string &name) const;

  /// Fetch the session associated with this request.
  core::Task<Session *> getSession();

  std::expected<size_t, ContentLengthError> getContentLength();

  std::string_view getContentType() const;

  /**
   * @brief get header with the given name. If multiple, returns the last one. Prefer `HttpRequest::getHeaderLower`.
   */
  std::string_view getHeader(const std::string &name) const;

  /**
   * @brief get header with the given name. If multiple, returns the last one
   *
   * @warning Lower means the provided KEY is lowercase, the header will be returned as is. All headers are stored with
   * a lowercase key
   */
  std::string_view getHeaderLower(const std::string &lowerKey) const;

  /// All headers with the given name
  std::vector<std::string> getHeaders(const std::string &name) const;

  /// All request headers
  std::vector<std::pair<std::string, std::string>> getAllHeaders() const;

  /**
   * @brief Set a header value (override if exists). Prefer `HttpRequest::setHeaderLower`.
   * The name will be lowercased before storing.
   */
  void setHeader(const std::string &name, const std::string &value);

  /// Set a header value (override if exists). @p key must be lowercase
  void setHeaderLower(const std::string_view &lowercaseKey, const std::string &value);

  /// Add a header value, ignores existing ones, use for multiple headers. Prefer `HttpRequest::addHeaderLower`
  void addHeader(const std::string &name, const std::string &value);
  /// Add a header value, ignores existing ones, use for multiple headers.
  void addHeaderLower(const std::string_view &lowercaseKey, const std::string &value);
  void addHeaderLower(const std::string_view &lowercaseKey, const std::string_view &value);

  /// Remove all instances of the header.
  void removeHeader(const std::string &name);

  /// Arbitrary data you may want to store on the request object. @p key is case sensitive
  void setAttribute(const std::string &key, const std::string &value);

  /// Retrieve stored attribute. @p key is case sensitive
  std::string getAttribute(const std::string &key, std::string defaultValue = "") const;

  /// Query parameters @p key is case sensitive
  std::string getQueryParam(const std::string &key, std::string defaultValue = "") const;

  /// get all values associated with a repeating query parameter.
  std::vector<std::string> getQueryParams(const std::string &key) const;

  std::vector<std::pair<std::string, std::string>> getAllQueryParams() const;

  /// Path parameters myPath/\<myPathParam\>/. @p key is case sensitive
  std::string getPathParam(const std::string &key, std::string defaultValue = "") const;

  std::vector<std::pair<std::string, std::string>> getAllPathParams() const;

  /**
   * @brief Consume the entire body stream as a string.
   *
   * @attention The body can only be consumed once. Subsequent calls will throw. If you wish to use the body data again
   * you must pass around the compy you get from this. Or if you must reattach it to the request, use @ref
   * setAttribute()
   *
   * @see bodyStream()
   * @see BodyStream
   */
  core::Task<std::string> consumeBody();

  /// get the body stream object. @see BodyStream
  BodyStream *bodyStream();

  /// Normalized path
  const std::string &getPath() const;
  /// Non-normalized path
  const std::string &getRawPath() const;
  /// HTTP version
  const std::string &getVersion() const;
  /// Path seperated by /
  const std::vector<std::string_view> &getPathParts() const;

  const std::string &getIp() const;
  uint16_t getPort() const;
  const std::string &getMethod() const;

  /**
   * @internalGroup
   * @{
   */
  void setPathParams(const std::vector<std::pair<std::string, std::string>> &pathParams); ///< @internalMethod
  void attachBodyStream(std::unique_ptr<BodyStream> bodyStream);                          ///< @internalMethod
  void setIp(const std::string &ip);                                                      ///< @internalMethod
  void setPort(uint16_t port);                                                            ///< @internalMethod
  void setMethod(const std::string &method);                                              ///< @internalMethod
  void setSessionHandle(SessionHandle *sessionHandle);                                    ///< @internalMethod
  void reset(const std::string &ip, uint16_t port);                                       ///< @internalMethod
  /** @} */

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
} // namespace rukh::http
