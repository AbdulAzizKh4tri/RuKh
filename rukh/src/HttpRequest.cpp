#include <rukh/HttpRequest.hpp>

#include <charconv>
#include <expected>
#include <memory>
#include <spdlog/spdlog.h>
#include <string_view>

#include <rukh/Exceptions.hpp>
#include <rukh/core/BodyStream.hpp>
#include <rukh/core/utils.hpp>
#include <rukh/middleware/SessionHandle.hpp>

namespace rukh {

using Range = std::pair<std::optional<size_t>, std::optional<size_t>>;
HttpRequest::HttpRequest() {}

bool HttpRequest::parseRequestHeader(std::string_view headerView) {
  auto lineEnd = headerView.find('\n');
  if (lineEnd == std::string_view::npos)
    return false;

  auto requestLine = headerView.substr(0, lineEnd);
  if (not requestLine.empty() && requestLine.back() == '\r')
    requestLine.remove_suffix(1);

  if (not parseRequestLine(requestLine))
    return false;

  headerView.remove_prefix(lineEnd + 1);
  if (not parseRequestHeaders(headerView))
    return false;
  return true;
}

Task<nlohmann::json> HttpRequest::jsonBody() {
  if (not toLowerCase(getContentType()).starts_with("application/json"))
    co_return {};
  co_return nlohmann::json::parse(co_await consumeBody());
}

Task<std::unordered_map<std::string, std::vector<std::string>>> HttpRequest::getFormData() {
  if (not toLowerCase(getContentType()).starts_with("application/x-www-form-urlencoded"))
    co_return {};
  std::unordered_map<std::string, std::vector<std::string>> formData;

  std::string body = co_await consumeBody();
  std::string_view bodyView = body;

  for (;;) {
    auto paramDelim = bodyView.find('&');
    auto pair = paramDelim == std::string_view::npos ? bodyView : bodyView.substr(0, paramDelim);

    if (pair.empty()) {
      if (paramDelim == std::string_view::npos)
        break;
      bodyView.remove_prefix(paramDelim + 1);
      continue;
    }

    auto eqpos = pair.find('=');
    if (eqpos != std::string_view::npos) {
      formData[percentDecode(pair.substr(0, eqpos))].push_back(percentDecode(pair.substr(eqpos + 1)));
    } else {
      formData.try_emplace(percentDecode(pair));
    }

    if (paramDelim == std::string_view::npos)
      break;

    bodyView.remove_prefix(paramDelim + 1);
  }
  co_return formData;
}

std::vector<HttpRequest::Range> HttpRequest::getRanges() const {
  auto rangeHeader = getHeaderLower("range");
  if (rangeHeader.empty())
    return {};

  std::vector<HttpRequest::Range> ranges;
  auto rangestart = rangeHeader.find("bytes=");
  if (rangestart == std::string_view::npos)
    return {};

  rangeHeader.remove_prefix(rangestart + 6);
  while (!rangeHeader.empty()) {
    auto rangeEnd = rangeHeader.find(',');
    auto range = rangeEnd == std::string_view::npos ? rangeHeader : rangeHeader.substr(0, rangeEnd);
    auto dash = range.find('-');
    if (dash == std::string_view::npos)
      return {};

    auto startStr = range.substr(0, dash);
    auto endStr = range.substr(dash + 1);
    std::optional<size_t> start, end;
    size_t tmp;

    auto [s_ptr, se] = std::from_chars(startStr.data(), startStr.data() + startStr.size(), tmp);
    if (se == std::errc{})
      start = tmp;

    auto [e_ptr, ee] = std::from_chars(endStr.data(), endStr.data() + endStr.size(), tmp);
    if (ee == std::errc{})
      end = tmp;

    if (!start && !end) {
      rangeHeader.remove_prefix(rangeEnd == std::string_view::npos ? rangeHeader.size() : rangeEnd + 1);
      continue;
    }

    ranges.emplace_back(start, end);

    rangeHeader.remove_prefix(rangeEnd == std::string_view::npos ? rangeHeader.size() : rangeEnd + 1);
  };

  return ranges;
}

std::vector<std::pair<std::string, std::string>> HttpRequest::getCookies() const {
  auto cookieHeader = getHeaderLower("cookie");
  if (cookieHeader.empty())
    return {};

  std::vector<std::pair<std::string, std::string>> cookies;

  for (;;) {
    auto cookieDelim = cookieHeader.find(';');
    auto cookiePair = cookieDelim == std::string_view::npos ? cookieHeader : cookieHeader.substr(0, cookieDelim);
    trim(cookiePair);
    auto eqpos = cookiePair.find('=');
    if (eqpos != std::string::npos) {
      cookies.emplace_back(cookiePair.substr(0, eqpos), cookiePair.substr(eqpos + 1));
    }

    if (cookieDelim == std::string_view::npos)
      break;

    cookieHeader.remove_prefix(cookieDelim + 1);
  }

  return cookies;
}

std::optional<std::string> HttpRequest::getCookie(const std::string &name) const {
  for (const auto &cookie : getCookies()) {
    if (cookie.first == name)
      return cookie.second;
  }
  return std::nullopt;
}

Task<Session *> HttpRequest::getSession() {
  if (not sessionHandle_)
    co_return nullptr;
  co_return co_await sessionHandle_->get();
}

std::expected<size_t, ContentLengthError> HttpRequest::getContentLength() {
  if (contentLength_.has_value())
    return *contentLength_;

  auto lenStr = getHeaderLower("content-length");
  if (lenStr.empty())
    return std::unexpected(ContentLengthError::NO_CONTENT_LENGTH_HEADER);

  size_t len;
  auto [ptr, ec] = std::from_chars(lenStr.data(), lenStr.data() + lenStr.size(), len);
  if (ec != std::errc{})
    return std::unexpected(ContentLengthError::INVALID_CONTENT_LENGTH);

  contentLength_ = len;
  return len;
}

std::string_view HttpRequest::getContentType() const {
  auto header = getHeaderLower("content-type");
  auto it = std::find(header.begin(), header.end(), ';');
  if (it != header.end())
    return std::string_view(header.begin(), it);
  else
    return header;
}

std::string_view HttpRequest::getHeader(const std::string &name) const {
  auto key = toLowerCase(name);
  auto it = std::find_if(headers_.rbegin(), headers_.rend(), [&key](const auto &p) { return p.first == key; });
  if (it != headers_.rend())
    return it->second;
  return {};
}

std::string_view HttpRequest::getHeaderLower(const std::string &lowerKey) const {
  auto it =
      std::find_if(headers_.rbegin(), headers_.rend(), [&lowerKey](const auto &p) { return p.first == lowerKey; });
  if (it != headers_.rend())
    return it->second;
  return {};
}

std::vector<std::string> HttpRequest::getHeaders(const std::string &name) const {
  return getAllValues(headers_, toLowerCase(name));
}

std::vector<std::pair<std::string, std::string>> HttpRequest::getAllHeaders() const { return headers_; }

void HttpRequest::setHeader(const std::string &name, const std::string &value) {
  std::string key = toLowerCase(name);
  std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
  headers_.emplace_back(key, value);
}

void HttpRequest::setHeaderLower(const std::string_view &lowercaseKey, const std::string &value) {
  std::erase_if(headers_, [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; });
  headers_.emplace_back(lowercaseKey, value);
}

void HttpRequest::addHeader(const std::string &name, const std::string &value) {
  auto key = toLowerCase(name);
  if (std::ranges::contains(singletonHeaders_, key)) {
    if (std::find_if(headers_.begin(), headers_.end(), [&key](const auto &p) { return p.first == key; }) ==
        headers_.end())
      headers_.emplace_back(key, value);
    return;
  }
  headers_.emplace_back(key, value);
}

void HttpRequest::addHeaderLower(const std::string_view &lowercaseKey, const std::string_view &value) {
  if (std::ranges::contains(singletonHeaders_, lowercaseKey)) {
    if (std::find_if(headers_.begin(), headers_.end(),
                     [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; }) == headers_.end())
      headers_.emplace_back(lowercaseKey, value);
    return;
  }
  headers_.emplace_back(lowercaseKey, value);
}

void HttpRequest::addHeaderLower(const std::string_view &lowercaseKey, const std::string &value) {
  if (std::ranges::contains(singletonHeaders_, lowercaseKey)) {
    if (std::find_if(headers_.begin(), headers_.end(),
                     [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; }) == headers_.end())
      headers_.emplace_back(lowercaseKey, value);
    return;
  }
  headers_.emplace_back(lowercaseKey, value);
}

void HttpRequest::removeHeader(const std::string &name) {
  auto key = toLowerCase(name);
  std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
}

void HttpRequest::setAttribute(const std::string &key, const std::string &value) {
  std::erase_if(attributes_, [&key](const auto &p) { return p.first == key; });
  attributes_.emplace_back(key, value);
}

std::string HttpRequest::getAttribute(const std::string &key, std::string defaultValue) const {
  return getLastOrDefault(attributes_, key, defaultValue);
}

std::string HttpRequest::getQueryParam(const std::string &key, std::string defaultValue) const {
  return getLastOrDefault(queryParams_, key, defaultValue);
}

std::vector<std::string> HttpRequest::getQueryParams(const std::string &key) const {
  return getAllValues(queryParams_, key);
}

std::vector<std::pair<std::string, std::string>> HttpRequest::getAllQueryParams() const { return queryParams_; }

std::string HttpRequest::getPathParam(const std::string &key, std::string defaultValue) const {
  return getLastOrDefault(pathParams_, key, defaultValue);
}

void HttpRequest::setPathParams(const std::vector<std::pair<std::string, std::string>> &pathParams) {
  pathParams_ = pathParams;
}

std::vector<std::pair<std::string, std::string>> HttpRequest::getAllPathParams() const { return pathParams_; }

Task<std::string> HttpRequest::consumeBody() {
  if (bodyStream_->isExhausted())
    throw HandlerException("Body stream is exhausted (Do not call fullBody() twice)", 500, true);
  std::string body;
  co_await bodyStream_->readAll(body);
  co_return body;
}

BodyStream *HttpRequest::bodyStream() { return bodyStream_.get(); }

void HttpRequest::attachBodyStream(std::unique_ptr<BodyStream> bodyStream) { bodyStream_ = std::move(bodyStream); }

const std::string &HttpRequest::getPath() const { return path_; }
const std::string &HttpRequest::getRawPath() const { return rawPath_; }
const std::string &HttpRequest::getVersion() const { return version_; }

const std::string &HttpRequest::getMethod() const { return method_; }
void HttpRequest::setMethod(const std::string &method) { method_ = method; }

const std::string &HttpRequest::getIp() const { return ip_; }
uint16_t HttpRequest::getPort() const { return port_; }

const std::vector<std::string_view> &HttpRequest::getPathParts() const { return pathParts_; }

void HttpRequest::setSessionHandle(SessionHandle *sessionHandle) { sessionHandle_ = sessionHandle; }

void HttpRequest::setIp(const std::string &ip) { ip_ = ip; }
void HttpRequest::setPort(uint16_t port) { port_ = port; }

bool HttpRequest::parseRequestLine(std::string_view requestLine) {
  auto method_end = requestLine.find(' ');
  if (method_end == std::string_view::npos)
    return false;

  method_ = requestLine.substr(0, method_end);
  requestLine.remove_prefix(method_end + 1);

  auto path_end = requestLine.find(' ');
  if (path_end == std::string_view::npos)
    return false;

  std::string_view rawPath = requestLine.substr(0, path_end);
  rawPath_ = rawPath;

  auto hostStart = rawPath.find("://");
  if (hostStart != std::string_view::npos) {
    rawPath.remove_prefix(hostStart + 3);
    // host ends at either '/' or '?' or end of string
    auto hostEnd = rawPath.find_first_of("/?");
    if (hostEnd != std::string_view::npos) {
      setHeaderLower("host", std::string(rawPath.substr(0, hostEnd)));
      rawPath.remove_prefix(hostEnd);
    } else {
      setHeaderLower("host", std::string(rawPath));
      rawPath = "/"; // host with no path — treat as root
    }
  }

  parsePathAndQueryParams(rawPath);
  requestLine.remove_prefix(path_end + 1);

  version_ = requestLine;
  return true;
}

bool HttpRequest::parseRequestHeaders(std::string_view headerView) {
  while (not headerView.empty()) {
    auto lineEnd = headerView.find('\n');
    auto line = lineEnd == std::string_view::npos ? headerView : headerView.substr(0, lineEnd);

    if (not line.empty() && (line.front() == ' ' || line.front() == '\t'))
      return false;

    headerView.remove_prefix(lineEnd == std::string_view::npos ? headerView.size() : lineEnd + 1);

    if (not line.empty() && line.back() == '\r')
      line.remove_suffix(1);

    auto pos = line.find(':');
    if (pos == std::string_view::npos)
      continue;

    auto key = line.substr(0, pos);
    auto value = line.substr(pos + 1);
    trim(value);
    std::string lowerKey(key.size(), '\0');
    std::transform(key.begin(), key.end(), lowerKey.begin(), [](unsigned char c) { return std::tolower(c); });
    addHeaderLower(lowerKey, value);
  }
  return true;
}

void HttpRequest::parsePathAndQueryParams(std::string_view rawPathView) {
  auto qpos = rawPathView.find('?');
  if (qpos == std::string_view::npos) {
    path_ = rawPathView;
    normalizePath(path_);
  } else {
    path_ = rawPathView.substr(0, qpos);
    normalizePath(path_);

    rawPathView.remove_prefix(qpos + 1);
    auto fragmentpos = rawPathView.find('#');
    if (fragmentpos != std::string_view::npos) {
      rawPathView.remove_suffix(rawPathView.size() - fragmentpos);
    }

    for (;;) {
      auto paramDelim = rawPathView.find('&');
      auto pair = paramDelim == std::string_view::npos ? rawPathView : rawPathView.substr(0, paramDelim);

      auto eqpos = pair.find('=');
      if (eqpos != std::string_view::npos) {
        const auto &val = percentDecode(pair.substr(eqpos + 1));
        queryParams_.emplace_back(percentDecode(pair.substr(0, eqpos)), val == "" ? "true" : val);
      } else {
        queryParams_.emplace_back(percentDecode(pair), "true");
      }

      if (paramDelim == std::string_view::npos)
        break;

      rawPathView.remove_prefix(paramDelim + 1);
    }
  }

  std::string_view remaining = path_;
  while (!remaining.empty()) {
    auto pos = remaining.find('/');
    if (pos == std::string_view::npos) {
      pathParts_.push_back(remaining);
      break;
    }
    if (pos > 0)
      pathParts_.push_back(remaining.substr(0, pos));
    remaining.remove_prefix(pos + 1);
  }
}

void HttpRequest::reset(const std::string &ip, uint16_t port) {
  headers_.clear(); // keeps capacity
  queryParams_.clear();
  attributes_.clear();
  pathParams_.clear();
  method_.clear();
  rawPath_.clear();
  path_.clear();
  version_.clear();
  pathParts_.clear();
  contentLength_ = std::nullopt;
  sessionHandle_ = nullptr;
  bodyStream_ = nullptr;
  ip_ = ip;
  port_ = port;
}
} // namespace rukh
